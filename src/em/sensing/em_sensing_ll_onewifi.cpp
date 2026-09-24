#include "em_sensing_ll_onewifi.h"

#include <cstdio>
#include <cstring>
#include <utility>

namespace {
constexpr size_t mac_string_size = 18U;

void format_mac(const mac_address_t mac, char (&output)[mac_string_size])
{
    std::snprintf(output, sizeof(output), "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
}

em_sensing_ll_onewifi_t::em_sensing_ll_onewifi_t()
    : m_delegate(true, false), m_csi_consumer(), m_csi_started(false), m_exchange_id(0U), m_client_mac{}, m_callback()
{
}

em_sensing_ll_onewifi_t::~em_sensing_ll_onewifi_t()
{
    stop_csi_consumer();
}

bool em_sensing_ll_onewifi_t::get_capabilities(em_sensing_capability_snapshot_t &capabilities) const
{
    return m_delegate.get_capabilities(capabilities);
}

bool em_sensing_ll_onewifi_t::start_csi_consumer(const mac_address_t client_mac)
{
    char client_mac_string[mac_string_size];

    if (client_mac == nullptr) {
        return false;
    }
    format_mac(client_mac, client_mac_string);
    std::memcpy(m_client_mac, client_mac, sizeof(m_client_mac));
    if (m_csi_started) {
        return wifi_csi_consumer_set_client_mac_list(&m_csi_consumer, client_mac_string) ==
            WIFI_CSI_CONSUMER_OK;
    }
    const int status = wifi_csi_consumer_start(&m_csi_consumer, client_mac_string,
        csi_sample_callback, this);
    m_csi_started = status == WIFI_CSI_CONSUMER_OK;
    return m_csi_started;
}

void em_sensing_ll_onewifi_t::stop_csi_consumer()
{
    if (m_csi_started) {
        (void)wifi_csi_consumer_stop(&m_csi_consumer);
        m_csi_started = false;
    }
}

int em_sensing_ll_onewifi_t::csi_sample_callback(const wifi_csi_sample_t *sample, void *user_data)
{
    if (sample == nullptr || user_data == nullptr) {
        return WIFI_CSI_CONSUMER_INVALID_ARGUMENT;
    }
    static_cast<em_sensing_ll_onewifi_t *>(user_data)->publish_csi_sample(*sample);
    return WIFI_CSI_CONSUMER_OK;
}

void em_sensing_ll_onewifi_t::publish_csi_sample(const wifi_csi_sample_t &sample)
{
    if (!m_callback || !m_csi_started || m_exchange_id == 0U) {
        return;
    }
    em_sensing_measurement_event_t event;
    event.exchange_id = m_exchange_id;
    event.data_type = EM_SENSING_DATA_TYPE_IEEE_CSI;
    std::memcpy(event.receiver, m_client_mac, sizeof(mac_address_t));
    event.data.resize(sizeof(sample.csi_data));
    std::memcpy(event.data.data(), &sample.csi_data, event.data.size());
    m_callback(event);
}

bool em_sensing_ll_onewifi_t::send_sensing_measurement_request(uint32_t exchange_id)
{
    m_exchange_id = exchange_id;
    return m_delegate.send_sensing_measurement_request(exchange_id);
}

bool em_sensing_ll_onewifi_t::send_sensing_measurement_request_frame(uint32_t exchange_id,
    const mac_address_t local, const mac_address_t peer, const em_sensing_measurement_params_t &params)
{
    m_exchange_id = exchange_id;
    if (!start_csi_consumer(peer)) {
        return false;
    }
    return m_delegate.send_sensing_measurement_request_frame(exchange_id, local, peer, params);
}

bool em_sensing_ll_onewifi_t::send_sensing_measurement_query(const mac_address_t agent_sta_mac,
    const mac_address_t bssid)
{
    return m_delegate.send_sensing_measurement_query(agent_sta_mac, bssid);
}

bool em_sensing_ll_onewifi_t::send_sensing_measurement_termination(uint32_t exchange_id)
{
    const bool result = m_delegate.send_sensing_measurement_termination(exchange_id);
    if (exchange_id == m_exchange_id) {
        m_exchange_id = 0U;
        stop_csi_consumer();
    }
    return result;
}

bool em_sensing_ll_onewifi_t::start_qos_null_exchange(uint32_t exchange_id)
{
    m_exchange_id = exchange_id;
    return m_delegate.start_qos_null_exchange(exchange_id);
}

bool em_sensing_ll_onewifi_t::stop_qos_null_exchange(uint32_t exchange_id)
{
    const bool result = m_delegate.stop_qos_null_exchange(exchange_id);
    if (exchange_id == m_exchange_id) {
        m_exchange_id = 0U;
        stop_csi_consumer();
    }
    return result;
}

bool em_sensing_ll_onewifi_t::sta_is_awake(const mac_address_t sta_mac) const
{
    return m_delegate.sta_is_awake(sta_mac);
}

bool em_sensing_ll_onewifi_t::send_qos_null_frame(uint32_t exchange_id, const mac_address_t bssid,
    const mac_address_t sta_mac, bool queue_for_power_save)
{
    m_exchange_id = exchange_id;
    if (!start_csi_consumer(sta_mac)) {
        return false;
    }
    return m_delegate.send_qos_null_frame(exchange_id, bssid, sta_mac, queue_for_power_save);
}

bool em_sensing_ll_onewifi_t::create_agent_sta_iface(const mac_address_t ruid, uint8_t count,
    std::vector<std::array<uint8_t, 6>> &mac_addresses)
{
    return m_delegate.create_agent_sta_iface(ruid, count, mac_addresses);
}

bool em_sensing_ll_onewifi_t::destroy_agent_sta_iface(const mac_address_t ruid)
{
    return m_delegate.destroy_agent_sta_iface(ruid);
}

bool em_sensing_ll_onewifi_t::send_probe_request(const mac_address_t agent_sta_mac,
    const std::vector<std::array<uint8_t, 6>> &bssids)
{
    return m_delegate.send_probe_request(agent_sta_mac, bssids);
}

bool em_sensing_ll_onewifi_t::agent_sta_is_unassociated_only() const
{
    return m_delegate.agent_sta_is_unassociated_only();
}

void em_sensing_ll_onewifi_t::set_event_callback(em_sensing_event_callback_t callback)
{
    m_callback = std::move(callback);
    m_delegate.set_event_callback(m_callback);
}
