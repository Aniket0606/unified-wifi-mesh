#include "em_sensing_receiver.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

em_sensing_receiver_t::em_sensing_receiver_t() : m_socket_fd(-1), m_ipv6(false) {}

em_sensing_receiver_t::~em_sensing_receiver_t() { close_socket(); }

bool em_sensing_receiver_t::bind_udp(uint16_t port, bool ipv6)
{
    close_socket();
    m_ipv6 = ipv6;
    m_socket_fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (m_socket_fd < 0) { return false; }
    if (ipv6) {
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_addr = in6addr_any;
        address.sin6_port = htons(port);
        if (bind(m_socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0) {
            close_socket();
            return false;
        }
    } else {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        address.sin_port = htons(port);
        if (bind(m_socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0) {
            close_socket();
            return false;
        }
    }
    return true;
}

bool em_sensing_receiver_t::receive_once(measurement_callback_t callback)
{
    if (m_socket_fd < 0 || callback == nullptr) { return false; }
    uint8_t buffer[65535] = {0};
    const ssize_t received = recv(m_socket_fd, buffer, sizeof(buffer), 0);
    if (received <= 0) { return false; }
    em_sensing_measurement_input_t measurement;
    std::vector<uint8_t> data;
    if (!em_sensing_l3_t::decode_measurement(buffer, static_cast<size_t>(received), measurement, data)) {
        return false;
    }
    callback(measurement, data);
    return true;
}

void em_sensing_receiver_t::close_socket()
{
    if (m_socket_fd >= 0) {
        close(m_socket_fd);
        m_socket_fd = -1;
    }
}
