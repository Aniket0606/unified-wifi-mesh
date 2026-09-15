#include <gtest/gtest.h>

#include "em_sensing.h"

TEST(em_sensing_agent_sta, validates_radio_and_count)
{
    em_sensing_t sensing;
    const mac_address_t ruid = {0, 0, 0, 0, 0, 0};
    EXPECT_TRUE(sensing.validate_agent_sta_iface_request(ruid, 0U));
    EXPECT_TRUE(sensing.validate_agent_sta_iface_request(ruid, EM_MAX_RADIO_PER_AGENT));
    const mac_address_t unknown = {1, 2, 3, 4, 5, 6};
    EXPECT_FALSE(sensing.validate_agent_sta_iface_request(unknown, 1U));
    EXPECT_FALSE(sensing.validate_agent_sta_iface_request(ruid, static_cast<uint8_t>(EM_MAX_RADIO_PER_AGENT + 1U)));
}

TEST(em_sensing_agent_sta, builds_empty_and_partial_reports)
{
    em_sensing_t sensing;
    const mac_address_t ruid = {0, 0, 0, 0, 0, 0};
    unsigned char buffer[256] = {0};
    EXPECT_GT(sensing.create_agent_sta_interface_report_tlv(buffer, ruid, {}), 0U);
    std::vector<std::array<uint8_t, 6>> partial = {{1, 2, 3, 4, 5, 6}};
    EXPECT_GT(sensing.create_agent_sta_interface_topology_tlv(buffer, ruid, partial), 0U);
}
