#include <gtest/gtest.h>

#include "em_sensing_l3.h"
#include "em_sensing_path.h"

#include <cstring>

TEST(em_sensing_l3, measurement_round_trip)
{
    const uint8_t data[] = {1U, 2U, 3U};
    em_sensing_measurement_input_t input;
    input.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
    input.exchange_id = 42U;
    input.transmitter[5] = 1U;
    input.receiver[5] = 2U;
    input.antenna_generation = 7U;
    input.data = data;
    input.data_length = sizeof(data);

    std::vector<uint8_t> payload;
    ASSERT_TRUE(em_sensing_l3_t::encode_measurement(input, payload));
    em_sensing_measurement_input_t decoded;
    std::vector<uint8_t> decoded_data;
    ASSERT_TRUE(em_sensing_l3_t::decode_measurement(payload.data(), payload.size(), decoded, decoded_data));
    EXPECT_EQ(decoded.exchange_id, input.exchange_id);
    EXPECT_EQ(decoded.data_type, input.data_type);
    EXPECT_EQ(decoded.antenna_generation, input.antenna_generation);
    EXPECT_EQ(decoded_data, std::vector<uint8_t>(data, data + sizeof(data)));
}

TEST(em_sensing_l3, rejects_invalid_lengths_and_wraps_generation)
{
    const uint8_t data[] = {1U};
    em_sensing_measurement_input_t input;
    input.data = data;
    input.data_length = sizeof(data);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(em_sensing_l3_t::encode_measurement(input, payload));
    em_sensing_measurement_input_t decoded;
    std::vector<uint8_t> output;
    EXPECT_FALSE(em_sensing_l3_t::decode_measurement(payload.data(), payload.size() - 1U, decoded, output));
    EXPECT_EQ(em_sensing_l3_t::next_antenna_generation(0xffffU, true, false), 0U);
    EXPECT_EQ(em_sensing_l3_t::next_antenna_generation(3U, false, false), 3U);
}

TEST(em_sensing_l3, rejects_unsupported_payload_version)
{
    const uint8_t data[] = {1U};
    em_sensing_measurement_input_t input;
    input.data = data;
    input.data_length = sizeof(data);
    std::vector<uint8_t> payload;
    ASSERT_TRUE(em_sensing_l3_t::encode_measurement(input, payload));
    auto *header = reinterpret_cast<em_layer3_path_payload_hdr_t *>(payload.data());
    header->version = htons(2U);

    em_sensing_measurement_input_t decoded;
    std::vector<uint8_t> output;
    EXPECT_FALSE(em_sensing_l3_t::decode_measurement(payload.data(), payload.size(), decoded, output));
}

TEST(em_sensing_l3, path_manager_preserves_configured_path)
{
    em_sensing_path_manager_t manager;
    em_layer3_path_setup_req_t request{};
    request.service_name = em_layer3_service_sensing;
    request.transport_protocol = em_layer3_transport_udp_ipv4;
    request.destination_address[12] = 192U;
    request.destination_address[13] = 0U;
    request.destination_address[14] = 2U;
    request.destination_address[15] = 1U;
    request.destination_port = 48100U;
    dm_layer3_path_info_t result;

    ASSERT_TRUE(manager.add_path(request, result));
    dm_layer3_path_info_t lookup;
    ASSERT_TRUE(manager.get_path(em_layer3_service_sensing, lookup));
    EXPECT_EQ(lookup.destination_port, request.destination_port);
    EXPECT_EQ(lookup.transport_protocol, request.transport_protocol);
    EXPECT_EQ(std::memcmp(lookup.destination_address, request.destination_address,
        sizeof(request.destination_address)), 0);
}

TEST(em_sensing_l3, path_manager_accepts_tcp_transport)
{
    em_sensing_path_manager_t manager;
    em_layer3_path_setup_req_t request{};
    request.service_name = em_layer3_service_sensing;
    request.transport_protocol = em_layer3_transport_tcp_ipv4;
    request.destination_address[12] = 192U;
    request.destination_address[13] = 0U;
    request.destination_address[14] = 2U;
    request.destination_address[15] = 1U;
    request.destination_port = 48100U;
    dm_layer3_path_info_t result;

    EXPECT_TRUE(manager.add_path(request, result));
    EXPECT_EQ(result.transport_protocol, em_layer3_transport_tcp_ipv4);
}