#include <gtest/gtest.h>

#include "em_sensing_tlv.h"

#include <array>
#include <cstring>
#include <vector>

namespace {

std::array<uint8_t, 6> make_mac(uint8_t base)
{
    std::array<uint8_t, 6> mac{};
    for (size_t index = 0U; index < mac.size(); ++index) {
        mac[index] = static_cast<uint8_t>(base + index);
    }
    return mac;
}

std::array<uint8_t, 6> make_array_mac(uint8_t base)
{
    std::array<uint8_t, 6> mac{};
    for (size_t index = 0U; index < mac.size(); ++index) {
        mac[index] = static_cast<uint8_t>(base + index);
    }
    return mac;
}

TEST(em_sensing_tlv, fixed_tlvs_round_trip)
{
    std::vector<uint8_t> encoded;

    em_sensing_exchange_req_t request{};
    request.exchange_id = 0x12345678U;
    request.flags = 0xc0U;
    request.exchange_type = em_sensing_exchange_tb;
    request.period = 10U;
    request.bandwidth = 80U;
    request.n_tx = 2U;
    request.n_rx = 4U;
    request.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
    const auto transmitter = make_mac(0x10U);
    const auto receiver = make_mac(0x20U);
    std::memcpy(request.transmitter, transmitter.data(), sizeof(mac_address_t));
    std::memcpy(request.receiver, receiver.data(), sizeof(mac_address_t));

    ASSERT_TRUE(em_encode_sensing_exchange_req_tlv(request, encoded));
    EXPECT_EQ(encoded.size(), 31U);
    em_sensing_exchange_req_t decoded_request{};
    ASSERT_TRUE(em_decode_sensing_exchange_req_tlv(encoded.data(), encoded.size(), decoded_request));
    EXPECT_EQ(decoded_request.exchange_id, request.exchange_id);
    EXPECT_EQ(decoded_request.period, request.period);
    EXPECT_EQ(decoded_request.bandwidth, request.bandwidth);
    EXPECT_EQ(std::memcmp(decoded_request.transmitter, request.transmitter, sizeof(mac_address_t)), 0);
    EXPECT_EQ(std::memcmp(decoded_request.receiver, request.receiver, sizeof(mac_address_t)), 0);

    em_sensing_exchange_rsp_t response{request.exchange_id, em_sensing_exchange_created};
    ASSERT_TRUE(em_encode_sensing_exchange_rsp_tlv(response, encoded));
    em_sensing_exchange_rsp_t decoded_response{};
    ASSERT_TRUE(em_decode_sensing_exchange_rsp_tlv(encoded.data(), encoded.size(), decoded_response));
    EXPECT_EQ(decoded_response.exchange_id, response.exchange_id);
    EXPECT_EQ(decoded_response.result_code, response.result_code);

    em_sensing_mq_req_t mq_request{};
    const auto agent_sta = make_mac(0x30U);
    const auto bssid = make_mac(0x40U);
    std::memcpy(mq_request.agent_sta_mac_addr, agent_sta.data(), sizeof(mac_address_t));
    std::memcpy(mq_request.bssid, bssid.data(), sizeof(mac_address_t));
    ASSERT_TRUE(em_encode_sensing_mq_req_tlv(mq_request, encoded));
    em_sensing_mq_req_t decoded_mq_request{};
    ASSERT_TRUE(em_decode_sensing_mq_req_tlv(encoded.data(), encoded.size(), decoded_mq_request));
    EXPECT_EQ(std::memcmp(decoded_mq_request.bssid, mq_request.bssid, sizeof(mac_address_t)), 0);

    em_sensing_mq_rsp_t mq_response{};
    std::memcpy(mq_response.agent_sta_mac_addr, mq_request.agent_sta_mac_addr, sizeof(mac_address_t));
    std::memcpy(mq_response.bssid, mq_request.bssid, sizeof(mac_address_t));
    mq_response.result_code = em_sensing_mq_timeout;
    ASSERT_TRUE(em_encode_sensing_mq_rsp_tlv(mq_response, encoded));
    em_sensing_mq_rsp_t decoded_mq_response{};
    ASSERT_TRUE(em_decode_sensing_mq_rsp_tlv(encoded.data(), encoded.size(), decoded_mq_response));
    EXPECT_EQ(decoded_mq_response.result_code, mq_response.result_code);
}

TEST(em_sensing_tlv, layer3_path_tlvs_round_trip)
{
    std::vector<uint8_t> encoded;
    em_layer3_path_setup_req_t request{};
    request.flags = 0x80U;
    request.service_name = em_layer3_service_sensing;
    request.transport_protocol = em_layer3_transport_udp_ipv6;
    request.destination_address[15] = 1U;
    request.destination_port = 5000U;
    ASSERT_TRUE(em_encode_layer3_path_setup_req_tlv(request, encoded));
    em_layer3_path_setup_req_t decoded_request{};
    ASSERT_TRUE(em_decode_layer3_path_setup_req_tlv(encoded.data(), encoded.size(), decoded_request));
    EXPECT_EQ(decoded_request.flags, request.flags);
    EXPECT_EQ(decoded_request.destination_port, request.destination_port);

    em_layer3_path_setup_rsp_t response{};
    response.service_name = em_layer3_service_sensing;
    response.result_code = 0U;
    response.source_address[15] = 2U;
    response.source_port = 5001U;
    ASSERT_TRUE(em_encode_layer3_path_setup_rsp_tlv(response, encoded));
    em_layer3_path_setup_rsp_t decoded_response{};
    ASSERT_TRUE(em_decode_layer3_path_setup_rsp_tlv(encoded.data(), encoded.size(), decoded_response));
    EXPECT_EQ(decoded_response.source_port, response.source_port);
    EXPECT_EQ(decoded_response.source_address[15], 2U);
}

TEST(em_sensing_tlv, variable_tlvs_round_trip)
{
    std::vector<em_sensing_radio_capability_view_t> capabilities(1U);
    const auto capability_ruid = make_mac(0x50U);
    std::memcpy(capabilities[0].ruid, capability_ruid.data(), sizeof(mac_address_t));
    capabilities[0].bss_flags = 0xe0U;
    capabilities[0].sta_flags = 0xa0U;
    capabilities[0].data_types = {EM_SENSING_DATA_TYPE_IEEE_CSI, 0x00112233U};
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(em_encode_sensing_cap_tlv(capabilities, encoded));
    std::vector<em_sensing_radio_capability_view_t> decoded_capabilities;
    ASSERT_TRUE(em_decode_sensing_cap_tlv(encoded.data(), encoded.size(), decoded_capabilities));
    ASSERT_EQ(decoded_capabilities.size(), 1U);
    EXPECT_EQ(decoded_capabilities[0].data_types, capabilities[0].data_types);
    EXPECT_EQ(decoded_capabilities[0].bss_flags, capabilities[0].bss_flags);

    std::vector<em_agent_sta_iface_radio_view_t> interfaces(1U);
    const auto interface_ruid = make_mac(0x60U);
    std::memcpy(interfaces[0].ruid, interface_ruid.data(), sizeof(mac_address_t));
    interfaces[0].agent_sta_mac_addresses = {make_array_mac(0x70U), make_array_mac(0x80U)};
    ASSERT_TRUE(em_encode_agent_sta_iface_tlv(interfaces, encoded));
    EXPECT_EQ(encoded.size(), 3U + 1U + 6U + 1U + 2U * 10U);
    std::vector<em_agent_sta_iface_radio_view_t> decoded_interfaces;
    ASSERT_TRUE(em_decode_agent_sta_iface_tlv(encoded.data(), encoded.size(), decoded_interfaces));
    ASSERT_EQ(decoded_interfaces.size(), 1U);
    EXPECT_EQ(decoded_interfaces[0].agent_sta_mac_addresses, interfaces[0].agent_sta_mac_addresses);

    em_trigger_probe_req_t probe{};
    const auto probe_sta = make_mac(0x90U);
    std::memcpy(probe.agent_sta_mac_addr, probe_sta.data(), sizeof(mac_address_t));
    std::vector<std::array<uint8_t, 6>> bssids = {make_array_mac(0xa0U), make_array_mac(0xb0U)};
    ASSERT_TRUE(em_encode_trigger_probe_req_tlv(probe, bssids, encoded));
    em_trigger_probe_req_t decoded_probe{};
    std::vector<std::array<uint8_t, 6>> decoded_bssids;
    ASSERT_TRUE(em_decode_trigger_probe_req_tlv(encoded.data(), encoded.size(), decoded_probe, decoded_bssids));
    EXPECT_EQ(decoded_probe.num_bssid, bssids.size());
    EXPECT_EQ(decoded_bssids, bssids);
}

TEST(em_sensing_tlv, malformed_tlvs_are_rejected)
{
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(em_encode_layer3_transport_cap_tlv(0x80U, encoded));
    uint8_t flags = 0U;
    EXPECT_TRUE(em_decode_layer3_transport_cap_tlv(encoded.data(), encoded.size(), flags));
    EXPECT_FALSE(em_decode_layer3_transport_cap_tlv(encoded.data(), encoded.size() - 1U, flags));
    encoded[2] = static_cast<uint8_t>(encoded[2] + 1U);
    EXPECT_FALSE(em_decode_layer3_transport_cap_tlv(encoded.data(), encoded.size(), flags));

    encoded = {static_cast<uint8_t>(em_tlv_type_trigger_probe_req), 0U, 7U, 1U, 2U, 3U, 4U, 5U, 6U, 2U};
    em_trigger_probe_req_t probe{};
    std::vector<std::array<uint8_t, 6>> bssids;
    EXPECT_FALSE(em_decode_trigger_probe_req_tlv(encoded.data(), encoded.size(), probe, bssids));

    std::vector<em_sensing_radio_capability_view_t> empty_capabilities;
    ASSERT_TRUE(em_encode_sensing_cap_tlv(empty_capabilities, encoded));
    std::vector<em_sensing_radio_capability_view_t> decoded_capabilities;
    EXPECT_TRUE(em_decode_sensing_cap_tlv(encoded.data(), encoded.size(), decoded_capabilities));

    std::vector<em_agent_sta_iface_radio_view_t> empty_interfaces;
    ASSERT_TRUE(em_encode_agent_sta_iface_tlv(empty_interfaces, encoded));
    std::vector<em_agent_sta_iface_radio_view_t> decoded_interfaces;
    EXPECT_TRUE(em_decode_agent_sta_iface_tlv(encoded.data(), encoded.size(), decoded_interfaces));
}

} // namespace
