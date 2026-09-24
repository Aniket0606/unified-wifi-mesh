#include "em_sensing_path.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool is_zero_address(const uint8_t *address)
{
    for (size_t index = 0U; index < 16U; ++index) {
        if (address[index] != 0U) {
            return false;
        }
    }
    return true;
}

bool is_multicast_or_loopback(const uint8_t *address, uint8_t protocol)
{
    if (protocol == em_layer3_transport_udp_ipv4 || protocol == em_layer3_transport_tcp_ipv4) {
        return address[12] == 127U || (address[12] >= 224U && address[12] <= 239U);
    }
    return address[0] == 0xffU || (address[0] == 0U && address[15] == 1U);
}

int socket_type(uint8_t protocol)
{
    return (protocol == em_layer3_transport_tcp_ipv6 || protocol == em_layer3_transport_tcp_ipv4) ?
        SOCK_STREAM : SOCK_DGRAM;
}

int address_family(uint8_t protocol)
{
    return (protocol == em_layer3_transport_udp_ipv4 || protocol == em_layer3_transport_tcp_ipv4) ?
        AF_INET : AF_INET6;
}

bool make_destination(uint8_t protocol, const uint8_t *address, uint16_t port,
    sockaddr_storage &destination, socklen_t &destination_length)
{
    if (protocol == em_layer3_transport_tcp_ipv4 || protocol == em_layer3_transport_udp_ipv4) {
        auto *socket_address = reinterpret_cast<sockaddr_in *>(&destination);
        socket_address->sin_family = AF_INET;
        std::memcpy(&socket_address->sin_addr.s_addr, address + 12U,
            sizeof(socket_address->sin_addr.s_addr));
        socket_address->sin_port = htons(port);
        destination_length = sizeof(sockaddr_in);
        return true;
    }
    if (protocol == em_layer3_transport_tcp_ipv6 || protocol == em_layer3_transport_udp_ipv6) {
        auto *socket_address = reinterpret_cast<sockaddr_in6 *>(&destination);
        socket_address->sin6_family = AF_INET6;
        std::memcpy(&socket_address->sin6_addr, address, sizeof(socket_address->sin6_addr));
        socket_address->sin6_port = htons(port);
        destination_length = sizeof(sockaddr_in6);
        return true;
    }
    return false;
}

bool connect_with_timeout(int fd, const sockaddr *destination, socklen_t destination_length)
{
    const int original_flags = fcntl(fd, F_GETFL, 0);
    if (original_flags < 0 || fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) < 0) {
        return false;
    }
    int result = connect(fd, destination, destination_length);
    if (result < 0 && errno == EINPROGRESS) {
        pollfd descriptor{fd, POLLOUT, 0};
        result = poll(&descriptor, 1, 1000);
        if (result > 0) {
            int socket_error = 0;
            socklen_t error_length = sizeof(socket_error);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) < 0) {
                result = -1;
            } else {
                result = socket_error;
            }
        } else {
            result = -1;
        }
    }
    const bool connected = result == 0 || (result < 0 && errno == EISCONN);
    (void)fcntl(fd, F_SETFL, original_flags);
    return connected;
}

bool resolve_udp_source(uint8_t protocol, const uint8_t *destination_address, uint16_t destination_port,
    uint8_t *source_address)
{
    const int probe_fd = socket(address_family(protocol), SOCK_DGRAM, 0);
    if (probe_fd < 0) {
        return false;
    }
    sockaddr_storage destination{};
    socklen_t destination_length = 0U;
    if (!make_destination(protocol, destination_address, destination_port, destination, destination_length) ||
        connect(probe_fd, reinterpret_cast<const sockaddr *>(&destination), destination_length) < 0) {
        close(probe_fd);
        return false;
    }
    sockaddr_storage local{};
    socklen_t local_length = sizeof(local);
    const bool ipv4 = protocol == em_layer3_transport_udp_ipv4;
    const bool valid = getsockname(probe_fd, reinterpret_cast<sockaddr *>(&local), &local_length) == 0;
    if (valid && ipv4) {
        const auto *address = reinterpret_cast<const sockaddr_in *>(&local);
        std::memset(source_address, 0, 16U);
        std::memcpy(source_address + 12U, &address->sin_addr.s_addr, sizeof(address->sin_addr.s_addr));
    } else if (valid) {
        const auto *address = reinterpret_cast<const sockaddr_in6 *>(&local);
        std::memcpy(source_address, &address->sin6_addr, 16U);
    }
    close(probe_fd);
    return valid;
}

} // namespace

em_sensing_path_manager_t::em_sensing_path_manager_t() = default;

em_sensing_path_manager_t::~em_sensing_path_manager_t()
{
    close_all();
}

bool em_sensing_path_manager_t::add_path(const em_layer3_path_setup_req_t &request,
    dm_layer3_path_info_t &result)
{
    if (m_paths.size() >= max_paths || request.destination_port == 0U ||
        request.transport_protocol > em_layer3_transport_tcp_ipv4 ||
        is_zero_address(request.destination_address) ||
        is_multicast_or_loopback(request.destination_address, request.transport_protocol)) {
        return false;
    }
    for (const auto &entry : m_paths) {
        if (entry.info.service_name == request.service_name &&
            entry.info.destination_port == request.destination_port &&
            entry.info.transport_protocol == request.transport_protocol &&
            std::memcmp(entry.info.destination_address, request.destination_address, 16U) == 0) {
            result = entry.info;
            return true;
        }
    }
    const int fd = socket(address_family(request.transport_protocol), socket_type(request.transport_protocol), 0);
    if (fd < 0) {
        return false;
    }
    const bool tcp = request.transport_protocol == em_layer3_transport_tcp_ipv4 ||
        request.transport_protocol == em_layer3_transport_tcp_ipv6;
    if (!tcp && fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return false;
    }
    sockaddr_storage local_address{};
    socklen_t local_address_length = 0U;
    if (request.transport_protocol == em_layer3_transport_udp_ipv4 ||
        request.transport_protocol == em_layer3_transport_tcp_ipv4) {
        auto *address = reinterpret_cast<sockaddr_in *>(&local_address);
        address->sin_family = AF_INET;
        address->sin_addr.s_addr = htonl(INADDR_ANY);
        address->sin_port = 0U;
        local_address_length = sizeof(sockaddr_in);
    } else {
        auto *address = reinterpret_cast<sockaddr_in6 *>(&local_address);
        address->sin6_family = AF_INET6;
        address->sin6_addr = in6addr_any;
        address->sin6_port = 0U;
        local_address_length = sizeof(sockaddr_in6);
    }
    if (bind(fd, reinterpret_cast<const sockaddr *>(&local_address), local_address_length) < 0) {
        close(fd);
        return false;
    }
    if (tcp) {
        sockaddr_storage destination{};
        socklen_t destination_length = 0U;
        if (!make_destination(request.transport_protocol, request.destination_address,
            request.destination_port, destination, destination_length) ||
            !connect_with_timeout(fd, reinterpret_cast<const sockaddr *>(&destination), destination_length)) {
            close(fd);
            return false;
        }
    }
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&local_address), &local_address_length) < 0) {
        close(fd);
        return false;
    }
    path_entry_t entry;
    entry.socket_fd = fd;
    entry.info.service_name = request.service_name;
    entry.info.transport_protocol = request.transport_protocol;
    std::memcpy(entry.info.destination_address, request.destination_address, 16U);
    entry.info.destination_port = request.destination_port;
    if (request.transport_protocol == em_layer3_transport_udp_ipv4 ||
        request.transport_protocol == em_layer3_transport_tcp_ipv4) {
        const auto *address = reinterpret_cast<const sockaddr_in *>(&local_address);
        entry.info.source_port = ntohs(address->sin_port);
        std::memset(entry.info.source_address, 0, sizeof(entry.info.source_address));
        std::memcpy(entry.info.source_address + 12U, &address->sin_addr.s_addr, sizeof(address->sin_addr.s_addr));
    } else {
        const auto *address = reinterpret_cast<const sockaddr_in6 *>(&local_address);
        entry.info.source_port = ntohs(address->sin6_port);
        std::memcpy(entry.info.source_address, &address->sin6_addr, sizeof(entry.info.source_address));
    }
    if (!tcp && !resolve_udp_source(request.transport_protocol, request.destination_address,
        request.destination_port, entry.info.source_address)) {
        close(fd);
        return false;
    }
    entry.info.active = true;
    m_paths.push_back(entry);
    result = entry.info;
    return true;
}

bool em_sensing_path_manager_t::remove_path(const em_layer3_path_setup_req_t &request,
    dm_layer3_path_info_t &result)
{
    for (auto iterator = m_paths.begin(); iterator != m_paths.end(); ++iterator) {
        if (iterator->info.service_name == request.service_name &&
            iterator->info.destination_port == request.destination_port &&
            iterator->info.transport_protocol == request.transport_protocol &&
            std::memcmp(iterator->info.destination_address, request.destination_address, 16U) == 0) {
            result = iterator->info;
            close(iterator->socket_fd);
            m_paths.erase(iterator);
            result.active = false;
            return true;
        }
    }
    return false;
}

bool em_sensing_path_manager_t::get_path(uint16_t service_name, dm_layer3_path_info_t &result) const
{
    for (const auto &entry : m_paths) {
        if (entry.info.active && entry.info.service_name == service_name) {
            result = entry.info;
            return true;
        }
    }
    return false;
}

bool em_sensing_path_manager_t::send_measurement(const dm_layer3_path_info_t &path,
    const em_sensing_measurement_input_t &measurement)
{
    std::vector<uint8_t> payload;
    if (!em_sensing_l3_t::encode_measurement(measurement, payload)) {
        return false;
    }
    for (const auto &entry : m_paths) {
        if (entry.info.service_name == path.service_name &&
            entry.info.transport_protocol == path.transport_protocol &&
            entry.info.destination_port == path.destination_port &&
            std::memcmp(entry.info.destination_address, path.destination_address, 16U) == 0) {
            if (path.transport_protocol == em_layer3_transport_tcp_ipv4 ||
                path.transport_protocol == em_layer3_transport_tcp_ipv6) {
                size_t sent_total = 0U;
                while (sent_total < payload.size()) {
                    const ssize_t sent = send(entry.socket_fd, payload.data() + sent_total,
                        payload.size() - sent_total, 0);
                    if (sent <= 0) {
                        return false;
                    }
                    sent_total += static_cast<size_t>(sent);
                }
                return true;
            }
            sockaddr_storage destination{};
            socklen_t destination_length = 0U;
            if (path.transport_protocol == em_layer3_transport_udp_ipv4) {
                auto *address = reinterpret_cast<sockaddr_in *>(&destination);
                address->sin_family = AF_INET;
                std::memcpy(&address->sin_addr.s_addr, path.destination_address + 12U,
                    sizeof(address->sin_addr.s_addr));
                address->sin_port = htons(path.destination_port);
                destination_length = sizeof(sockaddr_in);
            } else {
                auto *address = reinterpret_cast<sockaddr_in6 *>(&destination);
                address->sin6_family = AF_INET6;
                std::memcpy(&address->sin6_addr, path.destination_address,
                    sizeof(address->sin6_addr));
                address->sin6_port = htons(path.destination_port);
                destination_length = sizeof(sockaddr_in6);
            }
            const ssize_t sent = sendto(entry.socket_fd, payload.data(), payload.size(), 0,
                reinterpret_cast<const sockaddr *>(&destination), destination_length);
            return sent == static_cast<ssize_t>(payload.size());
        }
    }
    return false;
}

void em_sensing_path_manager_t::close_all()
{
    for (const auto &entry : m_paths) {
        if (entry.socket_fd >= 0) {
            close(entry.socket_fd);
        }
    }
    m_paths.clear();
}
