#include "em_sensing_path.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
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
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
        close(fd);
        return false;
    }
    path_entry_t entry;
    entry.socket_fd = fd;
    entry.info.service_name = request.service_name;
    entry.info.transport_protocol = request.transport_protocol;
    std::memcpy(entry.info.destination_address, request.destination_address, 16U);
    entry.info.destination_port = request.destination_port;
    entry.info.source_port = 0U;
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
            sockaddr_storage destination{};
            socklen_t destination_length = 0U;
            if (path.transport_protocol == em_layer3_transport_udp_ipv4 ||
                path.transport_protocol == em_layer3_transport_tcp_ipv4) {
                auto *address = reinterpret_cast<sockaddr_in *>(&destination);
                address->sin_family = AF_INET;
                std::memcpy(&address->sin_addr.s_addr, path.destination_address + 12U, sizeof(uint32_t));
                address->sin_port = htons(path.destination_port);
                destination_length = sizeof(sockaddr_in);
            } else {
                auto *address = reinterpret_cast<sockaddr_in6 *>(&destination);
                address->sin6_family = AF_INET6;
                std::memcpy(&address->sin6_addr, path.destination_address, 16U);
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
