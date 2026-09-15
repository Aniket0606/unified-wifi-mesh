#include "em_sensing_ll.h"

#include <algorithm>
#include <cstring>
#include <utility>

em_sensing_ll_stub_t::em_sensing_ll_stub_t(bool supported)
    : m_supported(supported), m_callback()
{
}

bool em_sensing_ll_stub_t::get_capabilities(em_sensing_capability_snapshot_t &capabilities) const
{
    capabilities = {};
    if (!m_supported) {
        return false;
    }

    capabilities.layer3_transport_flags = 0x80U;
    em_sensing_radio_capability_t radio;
    radio.bss_flags = 0xe0U;
    radio.sta_flags = 0xe0U;
    radio.data_types.push_back(EM_SENSING_DATA_TYPE_IEEE_CSI);
    capabilities.radios.push_back(radio);
    return true;
}

bool em_sensing_ll_stub_t::send_sensing_measurement_request(uint32_t exchange_id)
{
    if (!m_supported || exchange_id == 0U) {
        return false;
    }
    if (m_callback) {
        em_sensing_measurement_event_t event;
        event.exchange_id = exchange_id;
        event.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
        event.data = {0U};
        m_callback(event);
    }
    return true;
}

bool em_sensing_ll_stub_t::send_sensing_measurement_request_frame(uint32_t exchange_id,
    const mac_address_t local, const mac_address_t peer, const em_sensing_measurement_params_t &params)
{
    (void)params;
    if (!m_supported || exchange_id == 0U || local == nullptr || peer == nullptr) {
        return false;
    }
    if (m_callback) {
        em_sensing_measurement_event_t event;
        event.event_type = em_sensing_measurement_event_t::measurement_response;
        event.exchange_id = exchange_id;
        event.status_code = EM_SENSING_STATUS_SUCCESS;
        std::memcpy(event.transmitter, local, sizeof(mac_address_t));
        std::memcpy(event.receiver, peer, sizeof(mac_address_t));
        m_callback(event);
    }
    return true;
}

bool em_sensing_ll_stub_t::send_sensing_measurement_query(const mac_address_t agent_sta_mac,
    const mac_address_t bssid)
{
    return m_supported && agent_sta_mac != nullptr && bssid != nullptr;
}

bool em_sensing_ll_stub_t::send_sensing_measurement_termination(uint32_t exchange_id)
{
    return m_supported && exchange_id != 0U;
}

bool em_sensing_ll_stub_t::start_qos_null_exchange(uint32_t exchange_id)
{
    return m_supported && exchange_id != 0U;
}

bool em_sensing_ll_stub_t::stop_qos_null_exchange(uint32_t exchange_id)
{
    return m_supported && exchange_id != 0U;
}

bool em_sensing_ll_stub_t::sta_is_awake(const mac_address_t sta_mac) const
{
    return m_supported && sta_mac != nullptr;
}

bool em_sensing_ll_stub_t::send_qos_null_frame(uint32_t exchange_id, const mac_address_t bssid,
    const mac_address_t sta_mac, bool queue_for_power_save)
{
    (void)queue_for_power_save;
    if (!m_supported || exchange_id == 0U || bssid == nullptr || sta_mac == nullptr) {
        return false;
    }
    if (m_callback) {
        em_sensing_measurement_event_t event;
        event.exchange_id = exchange_id;
        event.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
        std::memcpy(event.transmitter, sta_mac, sizeof(mac_address_t));
        std::memcpy(event.receiver, bssid, sizeof(mac_address_t));
        event.data = {0U};
        m_callback(event);
    }
    return true;
}

bool em_sensing_ll_stub_t::create_agent_sta_iface(const mac_address_t ruid, uint8_t count,
    std::vector<std::array<uint8_t, 6>> &mac_addresses)
{
    mac_addresses.clear();
    if (!m_supported || ruid == nullptr || count == 0U) {
        return false;
    }

    for (uint8_t index = 0U; index < count; ++index) {
        std::array<uint8_t, 6> mac{};
        std::memcpy(mac.data(), ruid, mac.size());
        mac[5] = static_cast<uint8_t>(mac[5] + index + 1U);
        mac_addresses.push_back(mac);
    }
    return true;
}

bool em_sensing_ll_stub_t::destroy_agent_sta_iface(const mac_address_t ruid)
{
    return m_supported && ruid != nullptr;
}

bool em_sensing_ll_stub_t::send_probe_request(const mac_address_t agent_sta_mac,
    const std::vector<std::array<uint8_t, 6>> &bssids)
{
    return m_supported && agent_sta_mac != nullptr && bssids.size() <= UINT8_MAX;
}

void em_sensing_ll_stub_t::set_event_callback(em_sensing_event_callback_t callback)
{
    m_callback = std::move(callback);
}
