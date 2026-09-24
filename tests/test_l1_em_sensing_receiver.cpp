#include <gtest/gtest.h>

#include "em_sensing_receiver.h"
#include "em_sensing_l3.h"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>

TEST(em_sensing_receiver, binds_and_closes_udp_socket)
{
    em_sensing_receiver_t receiver;
    ASSERT_TRUE(receiver.bind_udp(0U, false));
    EXPECT_GE(receiver.socket_fd(), 0);
    receiver.close_socket();
    EXPECT_EQ(receiver.socket_fd(), -1);
}

TEST(em_sensing_receiver, receives_and_decodes_udp_measurement)
{
    em_sensing_receiver_t receiver;
    uint8_t loopback[16] = {};
    loopback[12] = 127U;
    loopback[15] = 1U;
    ASSERT_TRUE(receiver.bind_udp(0U, false, loopback));

    sockaddr_in receiver_address{};
    socklen_t receiver_address_length = sizeof(receiver_address);
    ASSERT_EQ(getsockname(receiver.socket_fd(), reinterpret_cast<sockaddr *>(&receiver_address),
        &receiver_address_length), 0);

    const uint8_t data[] = {4U, 5U};
    em_sensing_measurement_input_t input;
    input.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
    input.exchange_id = 9U;
    input.data = data;
    input.data_length = sizeof(data);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(em_sensing_l3_t::encode_measurement(input, payload));

    const int sender = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(sender, 0);
    ASSERT_EQ(sendto(sender, payload.data(), payload.size(), 0,
        reinterpret_cast<const sockaddr *>(&receiver_address), sizeof(receiver_address)),
        static_cast<ssize_t>(payload.size()));
    close(sender);
    pollfd descriptor{receiver.socket_fd(), POLLIN, 0};
    ASSERT_EQ(poll(&descriptor, 1, 1000), 1);

    bool received = false;
    ASSERT_TRUE(receiver.receive_once([&received](const em_sensing_measurement_input_t &measurement,
        const std::vector<uint8_t> &received_data) {
        received = measurement.exchange_id == 9U && received_data.size() == 2U &&
            received_data[0] == 4U && received_data[1] == 5U;
    }));
    EXPECT_TRUE(received);
}

TEST(em_sensing_receiver, reassembles_tcp_measurement_stream)
{
    em_sensing_receiver_t receiver;
    uint8_t loopback[16] = {};
    loopback[12] = 127U;
    loopback[15] = 1U;
    ASSERT_TRUE(receiver.bind_transport(0U, false, true, loopback));

    sockaddr_in receiver_address{};
    socklen_t receiver_address_length = sizeof(receiver_address);
    ASSERT_EQ(getsockname(receiver.socket_fd(), reinterpret_cast<sockaddr *>(&receiver_address),
        &receiver_address_length), 0);
    const int sender = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(sender, 0);
    ASSERT_EQ(connect(sender, reinterpret_cast<const sockaddr *>(&receiver_address),
        sizeof(receiver_address)), 0);

    const uint8_t data[] = {8U, 9U, 10U};
    em_sensing_measurement_input_t input;
    input.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
    input.exchange_id = 11U;
    input.data = data;
    input.data_length = sizeof(data);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(em_sensing_l3_t::encode_measurement(input, payload));

    const size_t split = payload.size() / 2U;
    ASSERT_EQ(send(sender, payload.data(), split, 0), static_cast<ssize_t>(split));
    EXPECT_FALSE(receiver.receive_once([](const em_sensing_measurement_input_t &,
        const std::vector<uint8_t> &) {}));
    ASSERT_EQ(send(sender, payload.data() + split, payload.size() - split, 0),
        static_cast<ssize_t>(payload.size() - split));

    bool received = false;
    for (unsigned int attempt = 0U; attempt < 3U && !received; ++attempt) {
        received = receiver.receive_once([&received](const em_sensing_measurement_input_t &measurement,
            const std::vector<uint8_t> &received_data) {
            received = measurement.exchange_id == 11U && received_data.size() == sizeof(data) &&
                std::memcmp(received_data.data(), data, sizeof(data)) == 0;
        });
    }
    close(sender);
    EXPECT_TRUE(received);
}