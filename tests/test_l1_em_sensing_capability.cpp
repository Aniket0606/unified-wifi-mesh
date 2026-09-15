#include <gtest/gtest.h>

#include "em_sensing.h"
#include "em_sensing_tlv.h"

#include <array>
#include <vector>

TEST(em_sensing_capability, creates_and_parses_transport_capability)
{
    em_sensing_t sensing;
    unsigned char value[64] = {0};
    const unsigned short length = sensing.create_layer3_transport_capability_tlv(value);
    ASSERT_EQ(length, 1U);

    std::vector<uint8_t> tlv = {
        static_cast<uint8_t>(em_tlv_type_layer3_transport_cap), 0U, 1U, value[0]};
    EXPECT_EQ(sensing.handle_sensing_capability_tlv(em_tlv_type_layer3_transport_cap,
        tlv.data(), tlv.size()), 0);
}

TEST(em_sensing_capability, creates_sensing_and_agent_sta_capabilities)
{
    em_sensing_t sensing;
    unsigned char value[256] = {0};
    const mac_address_t ruid = {0, 1, 2, 3, 4, 5};

    const unsigned short sensing_length = sensing.create_sensing_capability_tlv(value, ruid);
    ASSERT_GT(sensing_length, 0U);
    std::vector<uint8_t> sensing_tlv = {
        static_cast<uint8_t>(em_tlv_type_sensing_cap),
        static_cast<uint8_t>(sensing_length >> 8U), static_cast<uint8_t>(sensing_length & 0xffU)};
    sensing_tlv.insert(sensing_tlv.end(), value, value + sensing_length);
    EXPECT_EQ(sensing.handle_sensing_capability_tlv(em_tlv_type_sensing_cap,
        sensing_tlv.data(), sensing_tlv.size()), 0);

    const unsigned short agent_sta_length =
        sensing.create_agent_sta_interface_capability_tlv(value, ruid);
    ASSERT_GT(agent_sta_length, 0U);
    std::vector<uint8_t> agent_sta_tlv = {
        static_cast<uint8_t>(em_tlv_type_agent_sta_iface),
        static_cast<uint8_t>(agent_sta_length >> 8U), static_cast<uint8_t>(agent_sta_length & 0xffU)};
    agent_sta_tlv.insert(agent_sta_tlv.end(), value, value + agent_sta_length);
    EXPECT_EQ(sensing.handle_sensing_capability_tlv(em_tlv_type_agent_sta_iface,
        agent_sta_tlv.data(), agent_sta_tlv.size()), 0);
}
