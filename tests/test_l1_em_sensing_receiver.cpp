#include <gtest/gtest.h>

#include "em_sensing_receiver.h"

TEST(em_sensing_receiver, binds_and_closes_udp_socket)
{
    em_sensing_receiver_t receiver;
    ASSERT_TRUE(receiver.bind_udp(0U, false));
    EXPECT_GE(receiver.socket_fd(), 0);
    receiver.close_socket();
    EXPECT_EQ(receiver.socket_fd(), -1);
}