#include "em_sensing_l3.h"

#include <cstring>
#include <ctime>

namespace {
constexpr size_t layer3_header_size = sizeof(em_layer3_path_payload_hdr_t);
constexpr size_t sensing_header_size = sizeof(em_sensing_payload_hdr_t);
}

bool em_sensing_l3_t::encode_measurement(const em_sensing_measurement_input_t &measurement,
    std::vector<uint8_t> &payload)
{
    if (measurement.data_length > 0U && measurement.data == nullptr) {
        return false;
    }
    const size_t service_header_length = layer3_header_size + sensing_header_size;
    const size_t total_length = service_header_length + measurement.data_length;
    if (total_length > UINT32_MAX) {
        return false;
    }
    payload.assign(total_length, 0U);
    auto *common = reinterpret_cast<em_layer3_path_payload_hdr_t *>(payload.data());
    common->version = htons(1U);
    common->service_name = htons(em_layer3_service_sensing);
    common->service_header_len = htons(static_cast<uint16_t>(service_header_length));
    auto *header = reinterpret_cast<em_sensing_payload_hdr_t *>(payload.data() + layer3_header_size);
    struct timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    header->timestamp = (static_cast<unsigned long long>(now.tv_sec) << 32U) |
        static_cast<unsigned long long>(now.tv_nsec);
    header->data_type = htonl(measurement.data_type);
    header->exchange_id = htonl(measurement.exchange_id);
    std::memcpy(header->transmitter, measurement.transmitter, sizeof(mac_address_t));
    std::memcpy(header->receiver, measurement.receiver, sizeof(mac_address_t));
    header->antenna_generation = htons(measurement.antenna_generation);
    header->data_len = htonl(measurement.data_length);
    if (measurement.data_length > 0U) {
        std::memcpy(payload.data() + service_header_length, measurement.data, measurement.data_length);
    }
    return true;
}

bool em_sensing_l3_t::decode_measurement(const uint8_t *payload, size_t payload_length,
    em_sensing_measurement_input_t &measurement, std::vector<uint8_t> &data)
{
    if (payload == nullptr || payload_length < layer3_header_size + sensing_header_size) {
        return false;
    }
    const auto *common = reinterpret_cast<const em_layer3_path_payload_hdr_t *>(payload);
    if (ntohs(common->version) != 1U ||
        ntohs(common->service_name) != em_layer3_service_sensing ||
        ntohs(common->service_header_len) != layer3_header_size + sensing_header_size) {
        return false;
    }
    const auto *header = reinterpret_cast<const em_sensing_payload_hdr_t *>(payload + layer3_header_size);
    const uint32_t data_length = ntohl(header->data_len);
    const size_t header_length = ntohs(common->service_header_len);
    if (data_length != payload_length - header_length) {
        return false;
    }
    measurement.data_type = ntohl(header->data_type);
    measurement.exchange_id = ntohl(header->exchange_id);
    std::memcpy(measurement.transmitter, header->transmitter, sizeof(mac_address_t));
    std::memcpy(measurement.receiver, header->receiver, sizeof(mac_address_t));
    measurement.antenna_generation = ntohs(header->antenna_generation);
    measurement.data = nullptr;
    measurement.data_length = data_length;
    data.assign(payload + header_length, payload + payload_length);
    return true;
}

uint16_t em_sensing_l3_t::next_antenna_generation(uint16_t current_generation,
    bool transmitter_changed, bool receiver_changed)
{
    if (!transmitter_changed && !receiver_changed) {
        return current_generation;
    }
    return static_cast<uint16_t>(current_generation + 1U);
}