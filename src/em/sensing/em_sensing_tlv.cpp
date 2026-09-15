#include "em_sensing_tlv.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace {

constexpr size_t tlv_header_size = 3U;
constexpr size_t mac_address_size = sizeof(mac_address_t);
constexpr size_t ipv6_address_size = 16U;
constexpr size_t agent_sta_reserved_size = 4U;
constexpr uint8_t add_remove_mask = 0x80U;
constexpr uint8_t measurements_requested_mask = 0x40U;

void put_u16(std::vector<uint8_t> &buffer, uint16_t value)
{
    buffer.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
    buffer.push_back(static_cast<uint8_t>(value & 0xffU));
}

void put_u32(std::vector<uint8_t> &buffer, uint32_t value)
{
    buffer.push_back(static_cast<uint8_t>((value >> 24U) & 0xffU));
    buffer.push_back(static_cast<uint8_t>((value >> 16U) & 0xffU));
    buffer.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
    buffer.push_back(static_cast<uint8_t>(value & 0xffU));
}

bool get_u16(const uint8_t *data, size_t length, size_t &offset, uint16_t &value)
{
    if (data == nullptr || offset > length || length - offset < 2U) {
        return false;
    }
    value = static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8U) | data[offset + 1U]);
    offset += 2U;
    return true;
}

bool get_u32(const uint8_t *data, size_t length, size_t &offset, uint32_t &value)
{
    if (data == nullptr || offset > length || length - offset < 4U) {
        return false;
    }
    value = (static_cast<uint32_t>(data[offset]) << 24U) |
        (static_cast<uint32_t>(data[offset + 1U]) << 16U) |
        (static_cast<uint32_t>(data[offset + 2U]) << 8U) |
        static_cast<uint32_t>(data[offset + 3U]);
    offset += 4U;
    return true;
}

bool begin_tlv(em_tlv_type_t type, size_t value_length, std::vector<uint8_t> &tlv)
{
    if (value_length > UINT16_MAX) {
        return false;
    }
    tlv.clear();
    tlv.reserve(tlv_header_size + value_length);
    tlv.push_back(static_cast<uint8_t>(type));
    put_u16(tlv, static_cast<uint16_t>(value_length));
    return true;
}

bool get_value(const uint8_t *tlv, size_t tlv_len, em_tlv_type_t expected_type,
    const uint8_t **value, size_t &value_len)
{
    if (tlv == nullptr || value == nullptr || tlv_len < tlv_header_size ||
        tlv[0] != static_cast<uint8_t>(expected_type)) {
        return false;
    }
    const size_t declared_length = (static_cast<size_t>(tlv[1]) << 8U) | tlv[2];
    if (declared_length != tlv_len - tlv_header_size) {
        return false;
    }
    *value = tlv + tlv_header_size;
    value_len = declared_length;
    return true;
}

bool append_mac(std::vector<uint8_t> &buffer, const mac_address_t mac)
{
    buffer.insert(buffer.end(), mac, mac + mac_address_size);
    return true;
}

bool read_mac(const uint8_t *data, size_t length, size_t &offset, mac_address_t mac)
{
    if (data == nullptr || offset > length || length - offset < mac_address_size) {
        return false;
    }
    std::memcpy(mac, data + offset, mac_address_size);
    offset += mac_address_size;
    return true;
}

bool finish_fixed(const uint8_t *tlv, size_t tlv_len, em_tlv_type_t type, size_t expected_value_length,
    const uint8_t **value)
{
    size_t value_len = 0U;
    if (!get_value(tlv, tlv_len, type, value, value_len)) {
        return false;
    }
    return value_len == expected_value_length;
}

} // namespace

bool em_encode_layer3_transport_cap_tlv(uint8_t flags, std::vector<uint8_t> &tlv)
{
    if (!begin_tlv(em_tlv_type_layer3_transport_cap, 1U, tlv)) {
        return false;
    }
    tlv.push_back(static_cast<uint8_t>(flags & 0xf0U));
    return true;
}

bool em_decode_layer3_transport_cap_tlv(const uint8_t *tlv, size_t tlv_len, uint8_t &flags)
{
    const uint8_t *value = nullptr;
    if (!finish_fixed(tlv, tlv_len, em_tlv_type_layer3_transport_cap, 1U, &value) ||
        (value[0] & 0x0fU) != 0U) {
        return false;
    }
    flags = value[0];
    return true;
}

bool em_encode_sensing_exchange_req_tlv(const em_sensing_exchange_req_t &value, std::vector<uint8_t> &tlv)
{
    if (!begin_tlv(em_tlv_type_sensing_exchange_req, 28U, tlv)) {
        return false;
    }
    put_u32(tlv, value.exchange_id);
    tlv.push_back(static_cast<uint8_t>(value.flags & (add_remove_mask | measurements_requested_mask)));
    tlv.push_back(value.exchange_type);
    put_u16(tlv, value.period);
    put_u16(tlv, value.bandwidth);
    tlv.push_back(value.n_tx);
    tlv.push_back(value.n_rx);
    put_u32(tlv, value.data_type);
    append_mac(tlv, value.transmitter);
    append_mac(tlv, value.receiver);
    return true;
}

bool em_decode_sensing_exchange_req_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_exchange_req_t &value)
{
    const uint8_t *data = nullptr;
    size_t length = 0U;
    if (!get_value(tlv, tlv_len, em_tlv_type_sensing_exchange_req, &data, length) || length != 28U ||
        (data[4] & 0x3fU) != 0U || data[5] > em_sensing_exchange_non_tb) {
        return false;
    }
    size_t offset = 0U;
    if (!get_u32(data, length, offset, value.exchange_id)) {
        return false;
    }
    value.flags = data[offset++];
    value.exchange_type = data[offset++];
    if (!get_u16(data, length, offset, value.period) || !get_u16(data, length, offset, value.bandwidth) ||
        offset + 2U > length) {
        return false;
    }
    value.n_tx = data[offset++];
    value.n_rx = data[offset++];
    if (!get_u32(data, length, offset, value.data_type) ||
        !read_mac(data, length, offset, value.transmitter) || !read_mac(data, length, offset, value.receiver)) {
        return false;
    }
    return offset == length && value.exchange_type <= em_sensing_exchange_non_tb;
}

bool em_encode_sensing_exchange_rsp_tlv(const em_sensing_exchange_rsp_t &value, std::vector<uint8_t> &tlv)
{
    if (value.result_code > em_sensing_exchange_timeout || !begin_tlv(em_tlv_type_sensing_exchange_rsp, 5U, tlv)) {
        return false;
    }
    put_u32(tlv, value.exchange_id);
    tlv.push_back(value.result_code);
    return true;
}

bool em_decode_sensing_exchange_rsp_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_exchange_rsp_t &value)
{
    const uint8_t *data = nullptr;
    if (!finish_fixed(tlv, tlv_len, em_tlv_type_sensing_exchange_rsp, 5U, &data) || data[4] > em_sensing_exchange_timeout) {
        return false;
    }
    size_t offset = 0U;
    return get_u32(data, 5U, offset, value.exchange_id) &&
        (value.result_code = data[offset++], offset == 5U);
}

bool em_encode_layer3_path_setup_req_tlv(const em_layer3_path_setup_req_t &value, std::vector<uint8_t> &tlv)
{
    if (!begin_tlv(em_tlv_type_layer3_path_setup_req, 22U, tlv)) {
        return false;
    }
    tlv.push_back(static_cast<uint8_t>(value.flags & add_remove_mask));
    put_u16(tlv, value.service_name);
    tlv.push_back(value.transport_protocol);
    tlv.insert(tlv.end(), value.destination_address, value.destination_address + ipv6_address_size);
    put_u16(tlv, value.destination_port);
    return true;
}

bool em_decode_layer3_path_setup_req_tlv(const uint8_t *tlv, size_t tlv_len, em_layer3_path_setup_req_t &value)
{
    const uint8_t *data = nullptr;
    if (!finish_fixed(tlv, tlv_len, em_tlv_type_layer3_path_setup_req, 22U, &data) ||
        (data[0] & 0x7fU) != 0U || data[3] > em_layer3_transport_tcp_ipv4) {
        return false;
    }
    size_t offset = 0U;
    uint16_t port = 0U;
    value.flags = data[offset++];
    if (!get_u16(data, 22U, offset, value.service_name) || offset >= 22U) {
        return false;
    }
    value.transport_protocol = data[offset++];
    std::memcpy(value.destination_address, data + offset, ipv6_address_size);
    offset += ipv6_address_size;
    if (!get_u16(data, 22U, offset, port) || offset != 22U) {
        return false;
    }
    value.destination_port = port;
    return true;
}

bool em_encode_layer3_path_setup_rsp_tlv(const em_layer3_path_setup_rsp_t &value, std::vector<uint8_t> &tlv)
{
    if (!begin_tlv(em_tlv_type_layer3_path_setup_rsp, 21U, tlv)) {
        return false;
    }
    put_u16(tlv, value.service_name);
    tlv.push_back(value.result_code);
    tlv.insert(tlv.end(), value.source_address, value.source_address + ipv6_address_size);
    put_u16(tlv, value.source_port);
    return true;
}

bool em_decode_layer3_path_setup_rsp_tlv(const uint8_t *tlv, size_t tlv_len, em_layer3_path_setup_rsp_t &value)
{
    const uint8_t *data = nullptr;
    if (!finish_fixed(tlv, tlv_len, em_tlv_type_layer3_path_setup_rsp, 21U, &data)) {
        return false;
    }
    size_t offset = 0U;
    if (!get_u16(data, 21U, offset, value.service_name) || offset >= 21U) {
        return false;
    }
    value.result_code = data[offset++];
    std::memcpy(value.source_address, data + offset, ipv6_address_size);
    offset += ipv6_address_size;
    return get_u16(data, 21U, offset, value.source_port) && offset == 21U;
}

bool em_encode_sensing_mq_req_tlv(const em_sensing_mq_req_t &value, std::vector<uint8_t> &tlv)
{
    if (!begin_tlv(em_tlv_type_sensing_mq_req, 12U, tlv)) {
        return false;
    }
    append_mac(tlv, value.agent_sta_mac_addr);
    append_mac(tlv, value.bssid);
    return true;
}

bool em_decode_sensing_mq_req_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_mq_req_t &value)
{
    const uint8_t *data = nullptr;
    if (!finish_fixed(tlv, tlv_len, em_tlv_type_sensing_mq_req, 12U, &data)) {
        return false;
    }
    size_t offset = 0U;
    return read_mac(data, 12U, offset, value.agent_sta_mac_addr) && read_mac(data, 12U, offset, value.bssid) && offset == 12U;
}

bool em_encode_sensing_mq_rsp_tlv(const em_sensing_mq_rsp_t &value, std::vector<uint8_t> &tlv)
{
    if (value.result_code > em_sensing_mq_timeout || !begin_tlv(em_tlv_type_sensing_mq_rsp, 13U, tlv)) {
        return false;
    }
    append_mac(tlv, value.agent_sta_mac_addr);
    append_mac(tlv, value.bssid);
    tlv.push_back(value.result_code);
    return true;
}

bool em_decode_sensing_mq_rsp_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_mq_rsp_t &value)
{
    const uint8_t *data = nullptr;
    if (!finish_fixed(tlv, tlv_len, em_tlv_type_sensing_mq_rsp, 13U, &data) || data[12] > em_sensing_mq_timeout) {
        return false;
    }
    size_t offset = 0U;
    return read_mac(data, 13U, offset, value.agent_sta_mac_addr) && read_mac(data, 13U, offset, value.bssid) &&
        offset < 13U && (value.result_code = data[offset++], offset == 13U);
}

bool em_encode_sensing_cap_tlv(const std::vector<em_sensing_radio_capability_view_t> &value, std::vector<uint8_t> &tlv)
{
    if (value.size() > UINT8_MAX) {
        return false;
    }
    size_t length = 1U;
    for (const auto &radio : value) {
        if (radio.data_types.size() > UINT8_MAX) {
            return false;
        }
        length += 6U + 1U + 9U + 1U + 9U + 1U + (radio.data_types.size() * 4U);
    }
    if (!begin_tlv(em_tlv_type_sensing_cap, length, tlv)) {
        return false;
    }
    tlv.push_back(static_cast<uint8_t>(value.size()));
    for (const auto &radio : value) {
        append_mac(tlv, radio.ruid);
        tlv.push_back(static_cast<uint8_t>(radio.bss_flags & 0xe0U));
        tlv.insert(tlv.end(), radio.bss_capabilities.begin(), radio.bss_capabilities.end());
        tlv.push_back(static_cast<uint8_t>(radio.sta_flags & 0xe0U));
        tlv.insert(tlv.end(), radio.sta_capabilities.begin(), radio.sta_capabilities.end());
        tlv.push_back(static_cast<uint8_t>(radio.data_types.size()));
        for (uint32_t data_type : radio.data_types) {
            put_u32(tlv, data_type);
        }
    }
    return true;
}

bool em_decode_sensing_cap_tlv(const uint8_t *tlv, size_t tlv_len, std::vector<em_sensing_radio_capability_view_t> &value)
{
    const uint8_t *data = nullptr;
    size_t length = 0U;
    if (!get_value(tlv, tlv_len, em_tlv_type_sensing_cap, &data, length) || length < 1U) {
        return false;
    }
    size_t offset = 1U;
    const uint8_t num_radio = data[0];
    std::vector<em_sensing_radio_capability_view_t> parsed;
    parsed.reserve(num_radio);
    for (uint8_t index = 0U; index < num_radio; ++index) {
        em_sensing_radio_capability_view_t radio;
        if (!read_mac(data, length, offset, radio.ruid) || offset >= length) {
            return false;
        }
        radio.bss_flags = data[offset++];
        if (offset + radio.bss_capabilities.size() > length) {
            return false;
        }
        std::copy_n(data + offset, radio.bss_capabilities.size(), radio.bss_capabilities.begin());
        offset += radio.bss_capabilities.size();
        if (offset >= length) {
            return false;
        }
        radio.sta_flags = data[offset++];
        if (offset + radio.sta_capabilities.size() > length) {
            return false;
        }
        std::copy_n(data + offset, radio.sta_capabilities.size(), radio.sta_capabilities.begin());
        offset += radio.sta_capabilities.size();
        if (offset >= length) {
            return false;
        }
        const uint8_t num_data_types = data[offset++];
        if (static_cast<size_t>(num_data_types) > (length - offset) / 4U) {
            return false;
        }
        radio.data_types.reserve(num_data_types);
        for (uint8_t data_type = 0U; data_type < num_data_types; ++data_type) {
            uint32_t type = 0U;
            if (!get_u32(data, length, offset, type)) {
                return false;
            }
            radio.data_types.push_back(type);
        }
        parsed.push_back(radio);
    }
    if (offset != length) {
        return false;
    }
    value = std::move(parsed);
    return true;
}

bool em_encode_agent_sta_iface_tlv(const std::vector<em_agent_sta_iface_radio_view_t> &value, std::vector<uint8_t> &tlv)
{
    if (value.size() > UINT8_MAX) {
        return false;
    }
    size_t length = 1U;
    for (const auto &radio : value) {
        if (radio.agent_sta_mac_addresses.size() > UINT8_MAX) {
            return false;
        }
        length += 7U + radio.agent_sta_mac_addresses.size() * (mac_address_size + agent_sta_reserved_size);
    }
    if (!begin_tlv(em_tlv_type_agent_sta_iface, length, tlv)) {
        return false;
    }
    tlv.push_back(static_cast<uint8_t>(value.size()));
    for (const auto &radio : value) {
        append_mac(tlv, radio.ruid);
        tlv.push_back(static_cast<uint8_t>(radio.agent_sta_mac_addresses.size()));
        for (const auto &mac : radio.agent_sta_mac_addresses) {
            append_mac(tlv, mac.data());
            tlv.insert(tlv.end(), agent_sta_reserved_size, 0U);
        }
    }
    return true;
}

bool em_decode_agent_sta_iface_tlv(const uint8_t *tlv, size_t tlv_len, std::vector<em_agent_sta_iface_radio_view_t> &value)
{
    const uint8_t *data = nullptr;
    size_t length = 0U;
    if (!get_value(tlv, tlv_len, em_tlv_type_agent_sta_iface, &data, length) || length < 1U) {
        return false;
    }
    size_t offset = 1U;
    const uint8_t num_radio = data[0];
    std::vector<em_agent_sta_iface_radio_view_t> parsed;
    parsed.reserve(num_radio);
    for (uint8_t index = 0U; index < num_radio; ++index) {
        em_agent_sta_iface_radio_view_t radio;
        if (!read_mac(data, length, offset, radio.ruid) || offset >= length) {
            return false;
        }
        const uint8_t num_sta = data[offset++];
        if (static_cast<size_t>(num_sta) > (length - offset) / (mac_address_size + agent_sta_reserved_size)) {
            return false;
        }
        radio.agent_sta_mac_addresses.resize(num_sta);
        for (auto &mac : radio.agent_sta_mac_addresses) {
            if (!read_mac(data, length, offset, mac.data())) {
                return false;
            }
            if (offset + agent_sta_reserved_size > length) {
                return false;
            }
            offset += agent_sta_reserved_size;
        }
        parsed.push_back(radio);
    }
    if (offset != length) {
        return false;
    }
    value = std::move(parsed);
    return true;
}

bool em_encode_trigger_probe_req_tlv(const em_trigger_probe_req_t &value, const std::vector<std::array<uint8_t, 6>> &bssids, std::vector<uint8_t> &tlv)
{
    if (bssids.size() > UINT8_MAX) {
        return false;
    }
    if (!begin_tlv(em_tlv_type_trigger_probe_req, 7U + bssids.size() * mac_address_size, tlv)) {
        return false;
    }
    append_mac(tlv, value.agent_sta_mac_addr);
    tlv.push_back(static_cast<uint8_t>(bssids.size()));
    for (const auto &bssid : bssids) {
        append_mac(tlv, bssid.data());
    }
    return true;
}

bool em_decode_trigger_probe_req_tlv(const uint8_t *tlv, size_t tlv_len, em_trigger_probe_req_t &value, std::vector<std::array<uint8_t, 6>> &bssids)
{
    const uint8_t *data = nullptr;
    size_t length = 0U;
    if (!get_value(tlv, tlv_len, em_tlv_type_trigger_probe_req, &data, length) || length < 7U) {
        return false;
    }
    size_t offset = 0U;
    if (!read_mac(data, length, offset, value.agent_sta_mac_addr)) {
        return false;
    }
    const uint8_t num_bssid = data[offset++];
    if (static_cast<size_t>(num_bssid) > (length - offset) / mac_address_size) {
        return false;
    }
    std::vector<std::array<uint8_t, 6>> parsed(num_bssid);
    for (auto &bssid : parsed) {
        if (!read_mac(data, length, offset, bssid.data())) {
            return false;
        }
    }
    if (offset != length) {
        return false;
    }
    value.num_bssid = num_bssid;
    bssids = std::move(parsed);
    return true;
}
