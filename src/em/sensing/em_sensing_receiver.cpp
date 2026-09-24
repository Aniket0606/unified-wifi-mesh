#include "em_sensing_receiver.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

em_sensing_receiver_t::em_sensing_receiver_t()
    : m_socket_fd(-1), m_client_fd(-1), m_ipv6(false), m_tcp(false), m_tcp_buffer()
{
}

em_sensing_receiver_t::~em_sensing_receiver_t() { close_socket(); }

bool em_sensing_receiver_t::bind_udp(uint16_t port, bool ipv6, const uint8_t *bind_address)
{
    return bind_transport(port, ipv6, false, bind_address);
}

bool em_sensing_receiver_t::bind_transport(uint16_t port, bool ipv6, bool tcp,
    const uint8_t *bind_address)
{
    close_socket();
    m_ipv6 = ipv6;
    m_tcp = tcp;
    m_socket_fd = socket(ipv6 ? AF_INET6 : AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (m_socket_fd < 0) { return false; }
    if (fcntl(m_socket_fd, F_SETFL, O_NONBLOCK) < 0) {
        close_socket();
        return false;
    }
    if (ipv6) {
        sockaddr_in6 socket_address{};
        socket_address.sin6_family = AF_INET6;
        if (bind_address != nullptr) {
            std::memcpy(&socket_address.sin6_addr, bind_address, sizeof(socket_address.sin6_addr));
        } else {
            socket_address.sin6_addr = in6addr_any;
        }
        socket_address.sin6_port = htons(port);
        if (bind(m_socket_fd, reinterpret_cast<const sockaddr *>(&socket_address), sizeof(socket_address)) < 0) {
            close_socket();
            return false;
        }
    } else {
        sockaddr_in socket_address{};
        socket_address.sin_family = AF_INET;
        if (bind_address != nullptr) {
            std::memcpy(&socket_address.sin_addr.s_addr, bind_address + 12U,
                sizeof(socket_address.sin_addr.s_addr));
        } else {
            socket_address.sin_addr.s_addr = htonl(INADDR_ANY);
        }
        socket_address.sin_port = htons(port);
        if (bind(m_socket_fd, reinterpret_cast<const sockaddr *>(&socket_address), sizeof(socket_address)) < 0) {
            close_socket();
            return false;
        }
    }
    if (m_tcp && listen(m_socket_fd, 4) < 0) {
        close_socket();
        return false;
    }
    return true;
}

bool em_sensing_receiver_t::receive_once(measurement_callback_t callback)
{
    if (m_socket_fd < 0 || callback == nullptr) { return false; }
    if (m_tcp) {
        if (m_client_fd < 0) {
            m_client_fd = accept(m_socket_fd, nullptr, nullptr);
            if (m_client_fd < 0) {
                return false;
            }
            if (fcntl(m_client_fd, F_SETFL, O_NONBLOCK) < 0) {
                close(m_client_fd);
                m_client_fd = -1;
                return false;
            }
        }
        uint8_t chunk[65535];
        const ssize_t received = recv(m_client_fd, chunk, sizeof(chunk), 0);
        if (received == 0) {
            close(m_client_fd);
            m_client_fd = -1;
            m_tcp_buffer.clear();
            return false;
        } else if (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            close(m_client_fd);
            m_client_fd = -1;
            m_tcp_buffer.clear();
            return false;
        } else if (received > 0) {
            m_tcp_buffer.insert(m_tcp_buffer.end(), chunk, chunk + received);
        }
        if (m_tcp_buffer.size() < sizeof(em_layer3_path_payload_hdr_t)) {
            return false;
        }
        em_layer3_path_payload_hdr_t common{};
        std::memcpy(&common, m_tcp_buffer.data(), sizeof(common));
        const size_t service_header_length = ntohs(common.service_header_len);
        if (service_header_length < sizeof(common) + sizeof(em_sensing_payload_hdr_t) ||
            service_header_length > 1024U) {
            m_tcp_buffer.clear();
            return false;
        }
        if (m_tcp_buffer.size() < service_header_length) {
            return false;
        }
        uint32_t data_length_network = 0U;
        std::memcpy(&data_length_network,
            m_tcp_buffer.data() + service_header_length - sizeof(data_length_network),
            sizeof(data_length_network));
        const size_t frame_length = service_header_length + ntohl(data_length_network);
        if (frame_length > 65535U) {
            m_tcp_buffer.clear();
            return false;
        }
        if (m_tcp_buffer.size() < frame_length) {
            return false;
        }
        std::vector<uint8_t> frame(m_tcp_buffer.begin(), m_tcp_buffer.begin() + frame_length);
        m_tcp_buffer.erase(m_tcp_buffer.begin(), m_tcp_buffer.begin() + frame_length);
        // Aniket Need to improve this code
        em_sensing_measurement_input_t measurement;
        std::vector<uint8_t> data;
        if (!em_sensing_l3_t::decode_measurement(frame.data(), frame.size(), measurement, data)) {
            return false;
        }
        measurement.data = data.data();
        callback(measurement, data);
        return true;
    }
    uint8_t buffer[65535] = {0};
    const ssize_t received = recv(m_socket_fd, buffer, sizeof(buffer), 0);
    if (received <= 0) { return false; }
    // Aniket Need to improve this code
    em_sensing_measurement_input_t measurement;
    std::vector<uint8_t> data;
    if (!em_sensing_l3_t::decode_measurement(buffer, static_cast<size_t>(received), measurement, data)) {
        return false;
    }
    measurement.data = data.data();
    callback(measurement, data);
    return true;
}

void em_sensing_receiver_t::close_socket()
{
    if (m_client_fd >= 0) {
        close(m_client_fd);
        m_client_fd = -1;
    }
    if (m_socket_fd >= 0) {
        close(m_socket_fd);
        m_socket_fd = -1;
    }
    m_tcp_buffer.clear();
}
