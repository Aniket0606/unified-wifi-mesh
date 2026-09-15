/*
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#ifndef EM_SENSING_H
#define EM_SENSING_H

#include "em_base.h"
#include "em_sensing_ll.h"
#include "em_sensing_tlv.h"
#include "em_sensing_path.h"
#include "em_sensing_session.h"
#include "em_sensing_exchange.h"
#include <array>
#include <chrono>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

class em_sensing_t {
    using send_callback_t = std::function<int(unsigned char *, unsigned int)>;
    using path_result_callback_t = std::function<void(const dm_layer3_path_info_t &)>;
    using agent_sta_report_callback_t = std::function<void(const std::vector<em_agent_sta_iface_radio_view_t> &)>;
    using local_bss_callback_t = std::function<bool(const mac_address_t)>;
    using associated_sta_callback_t = std::function<bool(const mac_address_t, const mac_address_t)>;
    void handle_sensing_exchange_req(unsigned char *data, unsigned int len);
    void handle_sensing_exchange_rsp(unsigned char *data, unsigned int len);
    void handle_layer3_path_setup_req(unsigned char *data, unsigned int len);
    void handle_layer3_path_setup_rsp(unsigned char *data, unsigned int len);
    void handle_agent_sta_iface_config_req(unsigned char *data, unsigned int len);
    void handle_agent_sta_iface_config_rprt(unsigned char *data, unsigned int len);
    void handle_sensing_mq_req(unsigned char *data, unsigned int len);
    void handle_sensing_mq_rsp(unsigned char *data, unsigned int len);
    void handle_trigger_probe_req(unsigned char *data, unsigned int len);
    void handle_trigger_probe_req_rsp(unsigned char *data, unsigned int len);
    void handle_tunneled_probe_response(unsigned char *data, unsigned int len);
public:
    em_sensing_t();
    virtual ~em_sensing_t();

    void process_msg(unsigned char *data, unsigned int len);
    void process_agent_state();
    void process_ctrl_state();
    void set_send_callback(send_callback_t callback) { m_send_callback = std::move(callback); }
    void set_path_result_callback(path_result_callback_t callback) { m_path_result_callback = std::move(callback); }
    void set_agent_sta_report_callback(agent_sta_report_callback_t callback) { m_agent_sta_report_callback = std::move(callback); }
    void set_local_bss_callback(local_bss_callback_t callback) { m_local_bss_callback = std::move(callback); }
    void set_associated_sta_callback(associated_sta_callback_t callback) { m_associated_sta_callback = std::move(callback); }
    void set_agent_sta_interfaces(const std::vector<em_agent_sta_iface_radio_view_t> &interfaces)
    {
        m_agent_sta_interfaces = interfaces;
    }
    bool send_layer3_path_setup(const em_raw_hdr_t &route, const dm_layer3_path_info_t &path,
        bool add_path);
    bool send_agent_sta_iface_config(const em_raw_hdr_t &route, const mac_address_t ruid,
        uint8_t count);
    bool send_sensing_exchange(const em_raw_hdr_t &route, const em_sensing_exchange_req_t &request);
    bool send_sensing_mq(const em_raw_hdr_t &route, const em_sensing_mq_req_t &request);
    bool send_trigger_probe(const em_raw_hdr_t &route, const em_trigger_probe_req_t &request,
        const std::vector<std::array<uint8_t, 6>> &bssids);
    bool validate_agent_sta_iface_request(const mac_address_t ruid, uint8_t count) const;
    unsigned short create_agent_sta_interface_report_tlv(unsigned char *buffer,
        const mac_address_t ruid, const std::vector<std::array<uint8_t, 6>> &addresses) const;
    unsigned short create_agent_sta_interface_topology_tlv(unsigned char *buffer,
        const mac_address_t ruid, const std::vector<std::array<uint8_t, 6>> &addresses) const;
    bool create_session(std::string &socket_path) { return m_session_manager.create_session(socket_path); }
    bool delete_session(const std::string &socket_path) { return m_session_manager.delete_session(socket_path); }
    bool add_session_exchange(const std::string &socket_path, uint32_t exchange_id) { return m_session_manager.add_exchange(socket_path, exchange_id); }
    bool remove_session_exchange(const std::string &socket_path, uint32_t exchange_id) { return m_session_manager.remove_exchange(socket_path, exchange_id); }
    bool sensing_mq(const mac_address_t agent_sta_mac, const mac_address_t bssid);
    bool supports_data_type(const mac_address_t ruid, uint32_t data_type) const;
    bool supports_exchange_type(const mac_address_t ruid, uint8_t exchange_type) const;
    bool has_exchange(uint32_t exchange_id) const { return m_exchange_manager.contains(exchange_id); }
    void set_lower_layer(std::unique_ptr<em_sensing_ll_t> lower_layer);
    void process_qos_null_timers();
    void process_qos_null_timers_at(std::chrono::steady_clock::time_point now);
    void process_tb_timeouts_at(std::chrono::steady_clock::time_point now);
    bool has_pending_tb_query(uint32_t exchange_id) const
    {
        return m_tb_query_deadlines.find(exchange_id) != m_tb_query_deadlines.end();
    }
    void process_sensing_mq_timers_at(std::chrono::steady_clock::time_point now);
    uint8_t sensing_mq_result(const mac_address_t agent_sta_mac, const mac_address_t bssid) const;
    uint16_t last_trigger_probe_status() const { return m_last_trigger_probe_status; }
    const std::vector<std::array<uint8_t, 6>> &last_trigger_probe_failures() const
    {
        return m_last_trigger_probe_failures;
    }
    const std::vector<uint8_t> &last_tunneled_probe_response() const
    {
        return m_last_tunneled_probe_response;
    }

    bool get_sensing_capabilities(em_sensing_capability_snapshot_t &capabilities) const;
    bool sensing_supported() const;
    bool supports_sensing_exchange(const mac_address_t ruid, uint8_t exchange_type,
        uint32_t data_type) const;
    unsigned short create_layer3_transport_capability_tlv(unsigned char *buffer) const;
    unsigned short create_sensing_capability_tlv(unsigned char *buffer, const mac_address_t ruid) const;
    unsigned short create_agent_sta_interface_capability_tlv(unsigned char *buffer,
        const mac_address_t ruid) const;
    int handle_sensing_capability_tlv(em_tlv_type_t type, const unsigned char *buffer, unsigned int length);

private:
    std::unique_ptr<em_sensing_ll_t> m_lower_layer;
    em_sensing_capability_snapshot_t m_peer_capabilities;
    std::vector<em_agent_sta_iface_radio_view_t> m_peer_agent_sta_interfaces;
    std::vector<em_agent_sta_iface_radio_view_t> m_agent_sta_interfaces;
    em_sensing_path_manager_t m_path_manager;
    em_sensing_session_manager_t m_session_manager;
    em_sensing_exchange_manager_t m_exchange_manager;
    send_callback_t m_send_callback;
    path_result_callback_t m_path_result_callback;
    agent_sta_report_callback_t m_agent_sta_report_callback;
    local_bss_callback_t m_local_bss_callback;
    associated_sta_callback_t m_associated_sta_callback;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_qos_null_due_times;
    std::unordered_set<uint32_t> m_qos_null_measurements_pending;
    std::unordered_map<uint32_t, std::vector<uint8_t>> m_qos_null_request_frames;
    std::unordered_map<uint32_t, std::vector<uint8_t>> m_tb_request_frames;
    std::unordered_map<uint32_t, em_sensing_measurement_params_t> m_tb_pending_params;
    std::unordered_map<uint32_t, std::array<uint8_t, 12>> m_tb_pending_endpoints;
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> m_tb_query_deadlines;
    struct sensing_mq_pending_t {
        em_sensing_mq_req_t request{};
        std::vector<uint8_t> route_frame;
        std::chrono::steady_clock::time_point response_deadline;
        std::chrono::steady_clock::time_point expiry_deadline;
    };
    std::unordered_map<std::string, sensing_mq_pending_t> m_sensing_mq_pending;
    std::unordered_map<std::string, uint8_t> m_sensing_mq_results;
    uint16_t m_last_trigger_probe_status = 0U;
    std::vector<std::array<uint8_t, 6>> m_last_trigger_probe_failures;
    std::vector<uint8_t> m_last_tunneled_probe_response;

    bool send_ack(unsigned char *data, unsigned int len);
    void handle_lower_layer_event(const em_sensing_measurement_event_t &event);
    bool start_tb_exchange(const em_sensing_exchange_req_t &request, unsigned char *data, unsigned int len,
        uint8_t &result_code, bool allow_agent_sta = false);
    void handle_tb_measurement_query(uint32_t exchange_id);
    void handle_tb_measurement_response(uint32_t exchange_id, uint16_t status_code);
    void send_exchange_response_for(uint32_t exchange_id, uint8_t result_code);
    void clear_tb_state(uint32_t exchange_id);
    void handle_sensing_mq_measurement_request(const em_sensing_measurement_event_t &event);
    void handle_probe_response(const em_sensing_measurement_event_t &event);
    void send_trigger_probe_failure(unsigned char *data, unsigned int len,
        const em_trigger_probe_req_t &request, const std::vector<std::array<uint8_t, 6>> &bssids);
    void send_sensing_mq_response(const sensing_mq_pending_t &pending, uint8_t result_code);
    void arm_qos_null_timer(uint32_t exchange_id, uint16_t period);
    void stop_qos_null_timer(uint32_t exchange_id);
    void complete_qos_null_phase(uint32_t exchange_id);
    bool send_message(unsigned char *request, unsigned int request_len, em_msg_type_t message_type,
        em_tlv_type_t tlv_type, const unsigned char *value, unsigned short value_length);
};

#endif
