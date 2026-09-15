#ifndef EM_SENSING_RECEIVER_H
#define EM_SENSING_RECEIVER_H

#include "em_sensing_l3.h"

#include <functional>
#include <string>

class em_sensing_receiver_t {
public:
    using measurement_callback_t = std::function<void(const em_sensing_measurement_input_t &, const std::vector<uint8_t> &)>;

    em_sensing_receiver_t();
    ~em_sensing_receiver_t();

    bool bind_udp(uint16_t port, bool ipv6);
    bool receive_once(measurement_callback_t callback);
    void close_socket();
    int socket_fd() const { return m_socket_fd; }

private:
    int m_socket_fd;
    bool m_ipv6;
};

#endif
