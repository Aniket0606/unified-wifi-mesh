#include <gtest/gtest.h>

#include "em_sensing_l3.h"

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