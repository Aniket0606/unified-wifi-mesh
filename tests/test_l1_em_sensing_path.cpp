#include <gtest/gtest.h>

#include "em_sensing_path.h"

#include <arpa/inet.h>
#include <cstring>

TEST(em_sensing_path, creates_removes_and_limits_paths)
{
    em_sensing_path_manager_t manager;
    em_layer3_path_setup_req_t request{};
    request.service_name = em_layer3_service_sensing;
    request.transport_protocol = em_layer3_transport_udp_ipv4;
    request.destination_address[12] = 127U;
    request.destination_address[15] = 1U;
    request.destination_port = 5000U;
    dm_layer3_path_info_t result;
    EXPECT_FALSE(manager.add_path(request, result));

    request.destination_address[12] = 192U;
    request.destination_address[13] = 0U;
    request.destination_address[14] = 2U;
    request.destination_address[15] = 1U;
    ASSERT_TRUE(manager.add_path(request, result));
    EXPECT_TRUE(result.active);
    EXPECT_NE(result.source_port, 0U);
    EXPECT_EQ(manager.size(), 1U);
    ASSERT_TRUE(manager.remove_path(request, result));
    EXPECT_FALSE(result.active);
    EXPECT_EQ(manager.size(), 0U);
}

TEST(em_sensing_path, closes_all_paths)
{
    em_sensing_path_manager_t manager;
    em_layer3_path_setup_req_t request{};
    request.service_name = em_layer3_service_sensing;
    request.transport_protocol = em_layer3_transport_udp_ipv4;
    request.destination_address[12] = 192U;
    request.destination_address[13] = 0U;
    request.destination_address[14] = 2U;
    request.destination_address[15] = 2U;
    request.destination_port = 5001U;
    dm_layer3_path_info_t result;
    ASSERT_TRUE(manager.add_path(request, result));
    manager.close_all();
    EXPECT_EQ(manager.size(), 0U);
}