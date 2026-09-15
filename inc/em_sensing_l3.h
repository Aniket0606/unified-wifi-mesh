#ifndef EM_SENSING_L3_H
#define EM_SENSING_L3_H

#include "em_base.h"

#include <cstdint>
#include <vector>

struct em_sensing_measurement_input_t {
    uint32_t data_type = 0U;
    uint32_t exchange_id = 0U;
    mac_address_t transmitter{};
    mac_address_t receiver{};
    uint16_t antenna_generation = 0U;
    const uint8_t *data = nullptr;
    uint32_t data_length = 0U;
};

class em_sensing_l3_t {
public:
    static bool encode_measurement(const em_sensing_measurement_input_t &measurement,
        std::vector<uint8_t> &payload);
    static bool decode_measurement(const uint8_t *payload, size_t payload_length,
        em_sensing_measurement_input_t &measurement, std::vector<uint8_t> &data);
    static uint16_t next_antenna_generation(uint16_t current_generation,
        bool transmitter_changed, bool receiver_changed);
};

#endif