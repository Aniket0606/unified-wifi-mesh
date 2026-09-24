#ifndef EM_SENSING_LL_ONEWIFI_H
#define EM_SENSING_LL_ONEWIFI_H

#include "em_sensing_ll.h"
#include "wifi_csi_consumer.h"

class em_sensing_ll_onewifi_t final : public em_sensing_ll_t {
public:
    em_sensing_ll_onewifi_t();
    ~em_sensing_ll_onewifi_t() override;

    bool get_capabilities(em_sensing_capability_snapshot_t &capabilities) const override;
    bool send_sensing_measurement_request(uint32_t exchange_id) override;
    bool send_sensing_measurement_request_frame(uint32_t exchange_id, const mac_address_t local,
        const mac_address_t peer, const em_sensing_measurement_params_t &params) override;
    bool send_sensing_measurement_query(const mac_address_t agent_sta_mac, const mac_address_t bssid) override;
    bool send_sensing_measurement_termination(uint32_t exchange_id) override;
    bool start_qos_null_exchange(uint32_t exchange_id) override;
    bool stop_qos_null_exchange(uint32_t exchange_id) override;
    bool sta_is_awake(const mac_address_t sta_mac) const override;
    bool send_qos_null_frame(uint32_t exchange_id, const mac_address_t bssid,
        const mac_address_t sta_mac, bool queue_for_power_save) override;
    bool create_agent_sta_iface(const mac_address_t ruid, uint8_t count,
        std::vector<std::array<uint8_t, 6>> &mac_addresses) override;
    bool destroy_agent_sta_iface(const mac_address_t ruid) override;
    bool send_probe_request(const mac_address_t agent_sta_mac,
        const std::vector<std::array<uint8_t, 6>> &bssids) override;
    bool agent_sta_is_unassociated_only() const override;
    void set_event_callback(em_sensing_event_callback_t callback) override;

private:
    static int csi_sample_callback(const wifi_csi_sample_t *sample, void *user_data);
    bool start_csi_consumer(const mac_address_t client_mac);
    void stop_csi_consumer();
    void publish_csi_sample(const wifi_csi_sample_t &sample);

    em_sensing_ll_stub_t m_delegate;
    wifi_csi_consumer_t m_csi_consumer{};
    bool m_csi_started = false;
    uint32_t m_exchange_id = 0U;
    mac_address_t m_client_mac{};
    em_sensing_event_callback_t m_callback;
};

#endif
