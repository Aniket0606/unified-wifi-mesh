#ifndef EM_SENSING_LL_H
#define EM_SENSING_LL_H

#include "em_base.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

struct em_sensing_radio_capability_t {
    mac_address_t ruid{};
    uint8_t bss_flags = 0U;
    std::array<uint8_t, 9> bss_capabilities{};
    uint8_t sta_flags = 0U;
    std::array<uint8_t, 9> sta_capabilities{};
    std::vector<uint32_t> data_types;
};

struct em_sensing_capability_snapshot_t {
    uint8_t layer3_transport_flags = 0U;
    std::vector<em_sensing_radio_capability_t> radios;
};

struct em_sensing_measurement_event_t {
    enum event_type_t { measurement, exchange_terminated, station_disassociated,
        measurement_response, measurement_query, measurement_request, probe_response };
    event_type_t event_type = measurement;
    uint32_t exchange_id = 0U;
    uint8_t result_code = em_sensing_exchange_terminated;
    uint16_t status_code = 0U;
    uint8_t comeback_info = 0U;
    uint8_t comeback_after_exponent = 0U;
    uint8_t comeback_before_exponent = 0U;
    bool has_measurement_parameters = false;
    uint32_t data_type = 0U;
    mac_address_t transmitter{};
    mac_address_t receiver{};
    std::vector<uint8_t> data;
};

// Sensing Measurement Parameters field of an IEEE 802.11bf Sensing Measurement Request frame.
struct em_sensing_measurement_params_t {
    bool sensing_transmitter = false;
    bool sensing_receiver = false;
    bool report_requested = false;
    bool report_timestamp = false;
    uint16_t bandwidth = 0U;
    uint8_t tx_sts = 0U;
    uint8_t rx_sts = 0U;
    uint8_t num_rx_chains = 0U;
    uint16_t rsta_availability = 0U;
    uint8_t comeback_info = 0U;
};

static const uint16_t EM_SENSING_STATUS_SUCCESS = 0U;

using em_sensing_event_callback_t = std::function<void(const em_sensing_measurement_event_t &)>;

class em_sensing_ll_t {
public:
    virtual ~em_sensing_ll_t() = default;

    virtual bool get_capabilities(em_sensing_capability_snapshot_t &capabilities) const = 0;
    virtual bool send_sensing_measurement_request(uint32_t exchange_id) = 0;
    virtual bool send_sensing_measurement_request_frame(uint32_t exchange_id, const mac_address_t local,
        const mac_address_t peer, const em_sensing_measurement_params_t &params) = 0;
    virtual bool send_sensing_measurement_query(const mac_address_t agent_sta_mac, const mac_address_t bssid) = 0;
    virtual bool send_sensing_measurement_termination(uint32_t exchange_id) = 0;
    virtual bool start_qos_null_exchange(uint32_t exchange_id) = 0;
    virtual bool stop_qos_null_exchange(uint32_t exchange_id) = 0;
    virtual bool sta_is_awake(const mac_address_t sta_mac) const = 0;
    virtual bool send_qos_null_frame(uint32_t exchange_id, const mac_address_t bssid,
        const mac_address_t sta_mac, bool queue_for_power_save) = 0;
    virtual bool create_agent_sta_iface(const mac_address_t ruid, uint8_t count,
        std::vector<std::array<uint8_t, 6>> &mac_addresses) = 0;
    virtual bool destroy_agent_sta_iface(const mac_address_t ruid) = 0;
    virtual bool send_probe_request(const mac_address_t agent_sta_mac,
        const std::vector<std::array<uint8_t, 6>> &bssids) = 0;
    virtual bool agent_sta_is_unassociated_only() const = 0;
    virtual void set_event_callback(em_sensing_event_callback_t callback) = 0;
};

class em_sensing_ll_stub_t final : public em_sensing_ll_t {
public:
    explicit em_sensing_ll_stub_t(bool supported = true, bool emit_measurements = true);

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
    bool agent_sta_is_unassociated_only() const override { return true; }
    void set_event_callback(em_sensing_event_callback_t callback) override;

private:
    bool m_supported;
    bool m_emit_measurements;
    em_sensing_event_callback_t m_callback;
};

#endif
