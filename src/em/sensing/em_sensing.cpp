#include "em_sensing.h"
#include "em_sensing_tlv.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace {
std::string sensing_mq_key(const mac_address_t agent_sta_mac, const mac_address_t bssid)
{
    return std::string(reinterpret_cast<const char *>(agent_sta_mac), sizeof(mac_address_t)) +
        std::string(reinterpret_cast<const char *>(bssid), sizeof(mac_address_t));
}
}
#include <utility>

em_sensing_t::em_sensing_t()
    : m_lower_layer(new em_sensing_ll_stub_t(
#ifdef EM_SENSING_DISABLE_STUB
        false
#else
        true
#endif
    )), m_peer_capabilities(), m_peer_agent_sta_interfaces(), m_path_manager(), m_session_manager(), m_exchange_manager(),
    m_send_callback(), m_path_result_callback(), m_agent_sta_report_callback(), m_local_bss_callback(),
    m_associated_sta_callback(), m_qos_null_due_times(), m_qos_null_measurements_pending(),
    m_qos_null_request_frames()
{
    if (m_lower_layer != nullptr) {
        m_lower_layer->set_event_callback([this](const em_sensing_measurement_event_t &event) {
            handle_lower_layer_event(event);
        });
    }
}

em_sensing_t::~em_sensing_t() = default;

void em_sensing_t::set_lower_layer(std::unique_ptr<em_sensing_ll_t> lower_layer)
{
    m_lower_layer = std::move(lower_layer);
    if (m_lower_layer != nullptr) {
        m_lower_layer->set_event_callback([this](const em_sensing_measurement_event_t &event) {
            handle_lower_layer_event(event);
        });
    }
}

void em_sensing_t::handle_lower_layer_event(const em_sensing_measurement_event_t &event)
{
    if (event.event_type == em_sensing_measurement_event_t::probe_response) {
        handle_probe_response(event);
        return;
    }
    if (event.event_type == em_sensing_measurement_event_t::measurement_request) {
        handle_sensing_mq_measurement_request(event);
        return;
    }
    if (event.event_type == em_sensing_measurement_event_t::measurement_response) {
        handle_tb_measurement_response(event.exchange_id, event.status_code);
        return;
    }
    if (event.event_type == em_sensing_measurement_event_t::measurement_query) {
        handle_tb_measurement_query(event.exchange_id);
        return;
    }
    if (event.event_type == em_sensing_measurement_event_t::exchange_terminated ||
        event.event_type == em_sensing_measurement_event_t::station_disassociated) {
        stop_qos_null_timer(event.exchange_id);
        clear_tb_state(event.exchange_id);
        (void)m_exchange_manager.remove(event.exchange_id, em_sensing_exchange_terminated);
        (void)m_session_manager.notify_exchange_terminated(event.exchange_id,
            em_sensing_exchange_terminated);
        return;
    }
    em_sensing_measurement_input_t measurement;
    measurement.data_type = event.data_type;
    measurement.exchange_id = event.exchange_id;
    std::memcpy(measurement.transmitter, event.transmitter, sizeof(mac_address_t));
    std::memcpy(measurement.receiver, event.receiver, sizeof(mac_address_t));
    measurement.data = event.data.data();
    measurement.data_length = static_cast<uint32_t>(event.data.size());
    complete_qos_null_phase(event.exchange_id);
    dm_sensing_exchange_info_t exchange;
    if (m_exchange_manager.get(event.exchange_id, exchange) && exchange.measurements_requested) {
        dm_layer3_path_info_t path;
        path.service_name = em_layer3_service_sensing;
        path.active = true;
        (void)m_path_manager.send_measurement(path, measurement);
    }
    (void)m_session_manager.publish_measurement(measurement);
}

void em_sensing_t::handle_probe_response(const em_sensing_measurement_event_t &event)
{
    if (m_send_callback == nullptr || event.data.size() > 1020U) {
        return;
    }
    unsigned char buffer[1200] = {0};
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_tunneled);
    cmdu->id = htons(1U);
    cmdu->last_frag_ind = 1U;
    unsigned char *cursor = buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    auto *source_tlv = reinterpret_cast<em_tlv_t *>(cursor);
    source_tlv->type = em_tlv_type_src_info;
    source_tlv->len = htons(sizeof(em_source_info_t));
    auto *source = reinterpret_cast<em_source_info_t *>(source_tlv->value);
    std::memcpy(source->src_mac, event.transmitter, sizeof(mac_address_t));
    cursor += sizeof(em_tlv_t) + sizeof(em_source_info_t);
    auto *type_tlv = reinterpret_cast<em_tlv_t *>(cursor);
    type_tlv->type = em_tlv_type_tunneled_msg_type;
    type_tlv->len = htons(sizeof(em_tunneled_msg_type_t));
    auto *message_type = reinterpret_cast<em_tunneled_msg_type_t *>(type_tlv->value);
    message_type->msg_type = EM_TUNNELED_PROTOCOL_SENSING_PROBE_RESPONSE;
    cursor += sizeof(em_tlv_t) + sizeof(em_tunneled_msg_type_t);
    auto *tunneled_tlv = reinterpret_cast<em_tlv_t *>(cursor);
    tunneled_tlv->type = em_tlv_type_tunneled;
    tunneled_tlv->len = htons(static_cast<uint16_t>(sizeof(uint16_t) + event.data.size()));
    uint16_t body_length = htons(static_cast<uint16_t>(event.data.size()));
    std::memcpy(tunneled_tlv->value, &body_length, sizeof(body_length));
    std::memcpy(tunneled_tlv->value + sizeof(body_length), event.data.data(), event.data.size());
    cursor += sizeof(em_tlv_t) + sizeof(uint16_t) + event.data.size();
    auto *eom = reinterpret_cast<em_tlv_t *>(cursor);
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int length = static_cast<unsigned int>(cursor + sizeof(em_tlv_t) - buffer);
    (void)m_send_callback(buffer, length);
}

void em_sensing_t::send_trigger_probe_failure(unsigned char *data, unsigned int len,
    const em_trigger_probe_req_t &request, const std::vector<std::array<uint8_t, 6>> &bssids)
{
    if (m_send_callback == nullptr || data == nullptr) {
        return;
    }
    std::vector<uint8_t> probe_tlv;
    if (!em_encode_trigger_probe_req_tlv(request, bssids, probe_tlv)) {
        return;
    }
    unsigned char buffer[1200] = {0};
    const auto *request_header = reinterpret_cast<const em_raw_hdr_t *>(data);
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, request_header->src, sizeof(mac_address_t));
    std::memcpy(header->src, request_header->dst, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    const auto *request_cmdu = reinterpret_cast<const em_cmdu_t *>(data + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_trigger_probe_req_rsp);
    cmdu->id = request_cmdu->id;
    cmdu->last_frag_ind = 1U;
    auto *status_tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    status_tlv->type = em_tlv_type_status_code;
    status_tlv->len = htons(sizeof(em_status_code_t));
    auto *status = reinterpret_cast<em_status_code_t *>(status_tlv->value);
    status->status_code = htons(1U);
    auto *failed_tlv = reinterpret_cast<em_tlv_t *>(status_tlv->value + sizeof(em_status_code_t));
    std::memcpy(failed_tlv, probe_tlv.data(), probe_tlv.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(failed_tlv) + probe_tlv.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int message_length = static_cast<unsigned int>(reinterpret_cast<unsigned char *>(eom) +
        sizeof(em_tlv_t) - buffer);
    (void)m_send_callback(buffer, message_length);
}

void em_sensing_t::arm_qos_null_timer(uint32_t exchange_id, uint16_t period)
{
    const auto interval = std::chrono::microseconds(static_cast<uint32_t>(period) * 10240U);
    m_qos_null_due_times[exchange_id] = std::chrono::steady_clock::now() + interval;
}

void em_sensing_t::clear_tb_state(uint32_t exchange_id)
{
    m_tb_request_frames.erase(exchange_id);
    m_tb_pending_params.erase(exchange_id);
    m_tb_pending_endpoints.erase(exchange_id);
    m_tb_query_deadlines.erase(exchange_id);
}

void em_sensing_t::send_exchange_response_for(uint32_t exchange_id, uint8_t result_code)
{
    const auto request = m_tb_request_frames.find(exchange_id);
    if (request == m_tb_request_frames.end()) {
        return;
    }
    em_sensing_exchange_rsp_t response{};
    response.exchange_id = exchange_id;
    response.result_code = result_code;
    std::vector<uint8_t> response_tlv;
    if (!em_encode_sensing_exchange_rsp_tlv(response, response_tlv)) {
        return;
    }
    (void)send_message(request->second.data(), static_cast<unsigned int>(request->second.size()),
        em_msg_type_sensing_exchange_rsp, em_tlv_type_sensing_exchange_rsp,
        response_tlv.data() + sizeof(em_tlv_t),
        static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
}

bool em_sensing_t::start_tb_exchange(const em_sensing_exchange_req_t &request, unsigned char *data,
    unsigned int len, uint8_t &result_code, bool allow_agent_sta)
{
    if (m_lower_layer == nullptr || (!allow_agent_sta && m_local_bss_callback == nullptr)) {
        result_code = em_sensing_exchange_device_unavailable;
        return false;
    }
    const auto is_local_agent_sta = [this](const mac_address_t address) {
        for (const auto &radio : m_agent_sta_interfaces) {
            for (const auto &agent_sta : radio.agent_sta_mac_addresses) {
                if (std::memcmp(agent_sta.data(), address, sizeof(mac_address_t)) == 0) {
                    return true;
                }
            }
        }
        return false;
    };
    const bool local_is_transmitter = (m_local_bss_callback != nullptr &&
        m_local_bss_callback(request.transmitter)) ||
        (allow_agent_sta && is_local_agent_sta(request.transmitter));
    const bool local_is_receiver = !local_is_transmitter &&
        ((m_local_bss_callback != nullptr && m_local_bss_callback(request.receiver)) ||
        (allow_agent_sta && is_local_agent_sta(request.receiver)));
    if (!local_is_transmitter && !local_is_receiver) {
        result_code = em_sensing_exchange_device_unavailable;
        return false;
    }

    const unsigned char *local = local_is_transmitter ? request.transmitter : request.receiver;
    const unsigned char *peer = local_is_transmitter ? request.receiver : request.transmitter;

    em_sensing_measurement_params_t params;
    params.sensing_transmitter = !local_is_transmitter;
    params.sensing_receiver = local_is_transmitter;
    if (params.sensing_receiver && (request.flags & 0x40U) != 0U) {
        params.report_requested = true;
        params.report_timestamp = true;
    }
    params.bandwidth = request.bandwidth;
    params.tx_sts = request.n_tx;
    params.rx_sts = request.n_rx;
    params.num_rx_chains = request.n_rx;
    params.rsta_availability = request.period;

    std::array<uint8_t, 12> endpoints{};
    std::memcpy(endpoints.data(), local, sizeof(mac_address_t));
    std::memcpy(endpoints.data() + sizeof(mac_address_t), peer, sizeof(mac_address_t));
    m_tb_request_frames[request.exchange_id] = std::vector<uint8_t>(data, data + len);
    m_tb_pending_params[request.exchange_id] = params;
    m_tb_pending_endpoints[request.exchange_id] = endpoints;

    // Register before transmitting: the lower layer may report the response synchronously.
    dm_sensing_exchange_info_t exchange;
    exchange.exchange_id = request.exchange_id;
    exchange.exchange_type = request.exchange_type;
    exchange.add_exchange = true;
    exchange.measurements_requested = (request.flags & 0x40U) != 0U;
    exchange.period = request.period;
    exchange.bandwidth = request.bandwidth;
    exchange.data_type = request.data_type;
    std::memcpy(exchange.transmitter, request.transmitter, sizeof(mac_address_t));
    std::memcpy(exchange.receiver, request.receiver, sizeof(mac_address_t));
    (void)m_exchange_manager.add(exchange);

    const bool peer_associated = m_associated_sta_callback != nullptr &&
        m_associated_sta_callback(local, peer);
    if (!peer_associated) {
        m_tb_query_deadlines[request.exchange_id] = std::chrono::steady_clock::now() +
            std::chrono::seconds(10);
        result_code = em_sensing_exchange_created;
        return true;
    }

    if (!m_lower_layer->send_sensing_measurement_request_frame(request.exchange_id, local, peer, params)) {
        clear_tb_state(request.exchange_id);
        (void)m_exchange_manager.remove(request.exchange_id, em_sensing_exchange_device_unavailable);
        result_code = em_sensing_exchange_device_unavailable;
        return false;
    }
    result_code = em_sensing_exchange_created;
    return true;
}

void em_sensing_t::handle_tb_measurement_query(uint32_t exchange_id)
{
    const auto deadline = m_tb_query_deadlines.find(exchange_id);
    if (deadline == m_tb_query_deadlines.end() || m_lower_layer == nullptr) {
        return;
    }
    m_tb_query_deadlines.erase(deadline);
    const auto endpoints = m_tb_pending_endpoints.find(exchange_id);
    auto params = m_tb_pending_params.find(exchange_id);
    if (endpoints == m_tb_pending_endpoints.end() || params == m_tb_pending_params.end()) {
        return;
    }
    params->second.comeback_info = 0U;
    (void)m_lower_layer->send_sensing_measurement_request_frame(exchange_id, endpoints->second.data(),
        endpoints->second.data() + sizeof(mac_address_t), params->second);
}

void em_sensing_t::handle_tb_measurement_response(uint32_t exchange_id, uint16_t status_code)
{
    if (m_tb_request_frames.find(exchange_id) == m_tb_request_frames.end()) {
        return;
    }
    const uint8_t result_code = status_code == EM_SENSING_STATUS_SUCCESS ?
        em_sensing_exchange_created : em_sensing_exchange_request_declined;
    send_exchange_response_for(exchange_id, result_code);
    if (result_code != em_sensing_exchange_created) {
        (void)m_exchange_manager.remove(exchange_id, result_code);
    }
    clear_tb_state(exchange_id);
}

void em_sensing_t::process_tb_timeouts_at(std::chrono::steady_clock::time_point now)
{
    std::vector<uint32_t> expired;
    for (const auto &entry : m_tb_query_deadlines) {
        if (entry.second <= now) {
            expired.push_back(entry.first);
        }
    }
    for (const uint32_t exchange_id : expired) {
        send_exchange_response_for(exchange_id, em_sensing_exchange_timeout);
        (void)m_exchange_manager.remove(exchange_id, em_sensing_exchange_timeout);
        (void)m_session_manager.notify_exchange_terminated(exchange_id, em_sensing_exchange_timeout);
        clear_tb_state(exchange_id);
    }
}

void em_sensing_t::stop_qos_null_timer(uint32_t exchange_id)
{
    m_qos_null_due_times.erase(exchange_id);
    m_qos_null_measurements_pending.erase(exchange_id);
    m_qos_null_request_frames.erase(exchange_id);
}

void em_sensing_t::complete_qos_null_phase(uint32_t exchange_id)
{
    m_qos_null_measurements_pending.erase(exchange_id);
    dm_sensing_exchange_info_t exchange{};
    if (!m_exchange_manager.get(exchange_id, exchange) || exchange.period != 0U ||
        exchange.exchange_type != em_sensing_exchange_qos_null || m_lower_layer == nullptr) {
        return;
    }
    const auto request = m_qos_null_request_frames.find(exchange_id);
    (void)m_lower_layer->stop_qos_null_exchange(exchange_id);
    (void)m_exchange_manager.remove(exchange_id, em_sensing_exchange_terminated);
    (void)m_session_manager.notify_exchange_terminated(exchange_id, em_sensing_exchange_terminated);
    if (request != m_qos_null_request_frames.end()) {
        em_sensing_exchange_rsp_t response{};
        response.exchange_id = exchange_id;
        response.result_code = em_sensing_exchange_terminated;
        std::vector<uint8_t> response_tlv;
        if (em_encode_sensing_exchange_rsp_tlv(response, response_tlv)) {
            (void)send_message(request->second.data(), static_cast<unsigned int>(request->second.size()),
                em_msg_type_sensing_exchange_rsp, em_tlv_type_sensing_exchange_rsp,
                response_tlv.data() + sizeof(em_tlv_t),
                static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
        }
    }
    stop_qos_null_timer(exchange_id);
}

void em_sensing_t::process_qos_null_timers()
{
    process_qos_null_timers_at(std::chrono::steady_clock::now());
}

void em_sensing_t::process_qos_null_timers_at(std::chrono::steady_clock::time_point now)
{
    std::vector<uint32_t> due_exchanges;
    for (const auto &entry : m_qos_null_due_times) {
        if (entry.second <= now) {
            due_exchanges.push_back(entry.first);
        }
    }
    for (const uint32_t exchange_id : due_exchanges) {
        dm_sensing_exchange_info_t exchange{};
        if (!m_exchange_manager.get(exchange_id, exchange) ||
            exchange.exchange_type != em_sensing_exchange_qos_null || m_lower_layer == nullptr) {
            stop_qos_null_timer(exchange_id);
            continue;
        }
        const bool waiting_for_measurement = m_qos_null_measurements_pending.find(exchange.exchange_id) !=
            m_qos_null_measurements_pending.end();
        const bool sta_awake = m_lower_layer->sta_is_awake(exchange.transmitter);
        if (sta_awake || !waiting_for_measurement) {
            m_qos_null_measurements_pending.insert(exchange.exchange_id);
            if (!m_lower_layer->send_qos_null_frame(exchange.exchange_id, exchange.receiver,
                exchange.transmitter, !sta_awake)) {
                m_qos_null_measurements_pending.erase(exchange.exchange_id);
            }
        }
        const auto iterator = m_qos_null_due_times.find(exchange_id);
        if (iterator != m_qos_null_due_times.end() && exchange.period != 0U) {
            iterator->second = now + std::chrono::microseconds(static_cast<uint32_t>(exchange.period) * 10240U);
        }
    }
}

bool em_sensing_t::get_sensing_capabilities(em_sensing_capability_snapshot_t &capabilities) const
{
    return m_lower_layer != nullptr && m_lower_layer->get_capabilities(capabilities);
}

bool em_sensing_t::sensing_supported() const
{
    em_sensing_capability_snapshot_t capabilities;
    return get_sensing_capabilities(capabilities);
}

bool em_sensing_t::supports_data_type(const mac_address_t ruid, uint32_t data_type) const
{
    em_sensing_capability_snapshot_t capabilities;
    if (ruid == nullptr || !get_sensing_capabilities(capabilities)) { return false; }
    for (const auto &radio : capabilities.radios) {
        if (std::memcmp(radio.ruid, ruid, sizeof(mac_address_t)) == 0) {
            return std::find(radio.data_types.begin(), radio.data_types.end(), data_type) != radio.data_types.end();
        }
    }
    return false;
}

bool em_sensing_t::supports_exchange_type(const mac_address_t ruid, uint8_t exchange_type) const
{
    return supports_sensing_exchange(ruid, exchange_type, EM_SENSING_DATA_TYPE_IEEE_CSI);
}

bool em_sensing_t::sensing_mq(const mac_address_t agent_sta_mac, const mac_address_t bssid)
{
    return m_lower_layer != nullptr && agent_sta_mac != nullptr && bssid != nullptr &&
        m_lower_layer->send_sensing_measurement_query(agent_sta_mac, bssid);
}

uint8_t em_sensing_t::sensing_mq_result(const mac_address_t agent_sta_mac, const mac_address_t bssid) const
{
    if (agent_sta_mac == nullptr || bssid == nullptr) {
        return em_sensing_mq_timeout;
    }
    const auto result = m_sensing_mq_results.find(sensing_mq_key(agent_sta_mac, bssid));
    return result == m_sensing_mq_results.end() ? em_sensing_mq_timeout : result->second;
}

void em_sensing_t::process_sensing_mq_timers_at(std::chrono::steady_clock::time_point now)
{
    std::vector<std::pair<std::string, uint8_t>> expired;
    for (const auto &entry : m_sensing_mq_pending) {
        if (now >= entry.second.expiry_deadline) {
            expired.emplace_back(entry.first, em_sensing_mq_timeout);
        } else if (now >= entry.second.response_deadline) {
            expired.emplace_back(entry.first, em_sensing_mq_no_response);
        }
    }
    for (const auto &entry : expired) {
        const auto pending = m_sensing_mq_pending.find(entry.first);
        if (pending == m_sensing_mq_pending.end()) {
            continue;
        }
        send_sensing_mq_response(pending->second, entry.second);
        m_sensing_mq_results[entry.first] = entry.second;
        m_sensing_mq_pending.erase(pending);
    }
}

void em_sensing_t::send_sensing_mq_response(const sensing_mq_pending_t &pending, uint8_t result_code)
{
    if (pending.route_frame.empty()) {
        return;
    }
    em_sensing_mq_rsp_t response{};
    std::memcpy(response.agent_sta_mac_addr, pending.request.agent_sta_mac_addr, sizeof(mac_address_t));
    std::memcpy(response.bssid, pending.request.bssid, sizeof(mac_address_t));
    response.result_code = result_code;
    std::vector<uint8_t> encoded;
    if (em_encode_sensing_mq_rsp_tlv(response, encoded)) {
        (void)send_message(const_cast<unsigned char *>(pending.route_frame.data()),
            static_cast<unsigned int>(pending.route_frame.size()), em_msg_type_sensing_mq_rsp,
            em_tlv_type_sensing_mq_rsp, encoded.data() + sizeof(em_tlv_t),
            static_cast<unsigned short>(encoded.size() - sizeof(em_tlv_t)));
    }
}

void em_sensing_t::handle_sensing_mq_measurement_request(const em_sensing_measurement_event_t &event)
{
    const std::string key = sensing_mq_key(event.transmitter, event.receiver);
    const auto pending = m_sensing_mq_pending.find(key);
    if (pending == m_sensing_mq_pending.end()) {
        return;
    }
    if (event.comeback_info != 0U || !event.has_measurement_parameters) {
        if (std::chrono::steady_clock::now() < pending->second.expiry_deadline && m_lower_layer != nullptr) {
            (void)m_lower_layer->send_sensing_measurement_query(
                pending->second.request.agent_sta_mac_addr, pending->second.request.bssid);
            const uint8_t exponent = std::min<uint8_t>(event.comeback_after_exponent, 15U);
            const uint32_t comeback_tu = 1U << exponent;
            const auto comeback_delay = std::chrono::microseconds(comeback_tu * 1024U);
            const auto next_query = std::chrono::steady_clock::now() + comeback_delay;
            pending->second.response_deadline = std::min(next_query, pending->second.expiry_deadline);
        }
        return;
    }
    m_sensing_mq_results[key] = em_sensing_mq_success;
    send_sensing_mq_response(pending->second, em_sensing_mq_success);
    m_sensing_mq_pending.erase(pending);
}

bool em_sensing_t::supports_sensing_exchange(const mac_address_t ruid, uint8_t exchange_type,
    uint32_t data_type) const
{
    em_sensing_capability_snapshot_t capabilities;
    if (ruid == nullptr || !get_sensing_capabilities(capabilities)) {
        return false;
    }
    for (const auto &radio : capabilities.radios) {
        if (std::memcmp(radio.ruid, ruid, sizeof(mac_address_t)) != 0) {
            continue;
        }
        const bool type_supported =
            (exchange_type == em_sensing_exchange_qos_null) ||
            (exchange_type == em_sensing_exchange_tb && (radio.bss_flags & 0x40U) != 0U) ||
            (exchange_type == em_sensing_exchange_non_tb && (radio.sta_flags & 0x20U) != 0U);
        const bool data_supported = std::find(radio.data_types.begin(), radio.data_types.end(), data_type) !=
            radio.data_types.end();
        return type_supported && data_supported;
    }
    return false;
}

unsigned short em_sensing_t::create_layer3_transport_capability_tlv(unsigned char *buffer) const
{
    if (buffer == nullptr) {
        return 0U;
    }
    em_sensing_capability_snapshot_t capabilities;
    if (!get_sensing_capabilities(capabilities)) {
        return 0U;
    }
    std::vector<uint8_t> encoded;
    if (!em_encode_layer3_transport_cap_tlv(capabilities.layer3_transport_flags, encoded)) {
        return 0U;
    }
    std::memcpy(buffer, encoded.data() + sizeof(em_tlv_t), encoded.size() - sizeof(em_tlv_t));
    return static_cast<unsigned short>(encoded.size() - sizeof(em_tlv_t));
}

unsigned short em_sensing_t::create_sensing_capability_tlv(unsigned char *buffer, const mac_address_t ruid) const
{
    if (buffer == nullptr || ruid == nullptr) {
        return 0U;
    }
    em_sensing_capability_snapshot_t capabilities;
    if (!get_sensing_capabilities(capabilities)) {
        return 0U;
    }
    std::vector<em_sensing_radio_capability_view_t> radios;
    for (const auto &radio : capabilities.radios) {
        em_sensing_radio_capability_view_t view;
        std::memcpy(view.ruid, ruid, sizeof(mac_address_t));
        view.bss_flags = radio.bss_flags;
        view.bss_capabilities = radio.bss_capabilities;
        view.sta_flags = radio.sta_flags;
        view.sta_capabilities = radio.sta_capabilities;
        view.data_types = radio.data_types;
        radios.push_back(view);
    }
    std::vector<uint8_t> encoded;
    if (!em_encode_sensing_cap_tlv(radios, encoded)) {
        return 0U;
    }
    std::memcpy(buffer, encoded.data() + sizeof(em_tlv_t), encoded.size() - sizeof(em_tlv_t));
    return static_cast<unsigned short>(encoded.size() - sizeof(em_tlv_t));
}

unsigned short em_sensing_t::create_agent_sta_interface_capability_tlv(unsigned char *buffer,
    const mac_address_t ruid) const
{
    if (buffer == nullptr || ruid == nullptr || !sensing_supported()) {
        return 0U;
    }
    em_agent_sta_iface_radio_view_t radio;
    std::memcpy(radio.ruid, ruid, sizeof(mac_address_t));
    std::vector<em_agent_sta_iface_radio_view_t> radios = {radio};
    std::vector<uint8_t> encoded;
    if (!em_encode_agent_sta_iface_tlv(radios, encoded)) {
        return 0U;
    }
    std::memcpy(buffer, encoded.data() + sizeof(em_tlv_t), encoded.size() - sizeof(em_tlv_t));
    return static_cast<unsigned short>(encoded.size() - sizeof(em_tlv_t));
}

int em_sensing_t::handle_sensing_capability_tlv(em_tlv_type_t type, const unsigned char *buffer,
    unsigned int length)
{
    if (buffer == nullptr || length < sizeof(em_tlv_t)) {
        return -1;
    }
    if (type == em_tlv_type_layer3_transport_cap) {
        uint8_t flags = 0U;
        if (!em_decode_layer3_transport_cap_tlv(buffer, length, flags)) {
            return -1;
        }
        m_peer_capabilities.layer3_transport_flags = flags;
        return 0;
    }
    if (type == em_tlv_type_sensing_cap) {
        std::vector<em_sensing_radio_capability_view_t> radios;
        if (!em_decode_sensing_cap_tlv(buffer, length, radios)) {
            return -1;
        }
        m_peer_capabilities.radios.clear();
        for (const auto &radio : radios) {
            em_sensing_radio_capability_t capability;
            std::memcpy(capability.ruid, radio.ruid, sizeof(mac_address_t));
            capability.bss_flags = radio.bss_flags;
            capability.bss_capabilities = radio.bss_capabilities;
            capability.sta_flags = radio.sta_flags;
            capability.sta_capabilities = radio.sta_capabilities;
            capability.data_types = radio.data_types;
            m_peer_capabilities.radios.push_back(capability);
        }
        return 0;
    }
    if (type == em_tlv_type_agent_sta_iface) {
        if (!em_decode_agent_sta_iface_tlv(buffer, length, m_peer_agent_sta_interfaces)) {
            return -1;
        }
        return 0;
    }
    return -1;
}

void em_sensing_t::process_msg(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) {
        return;
    }
    const em_cmdu_t *cmdu = reinterpret_cast<const em_cmdu_t *>(data + sizeof(em_raw_hdr_t));
    switch (htons(cmdu->type)) {
        case em_msg_type_sensing_exchange_req:
            handle_sensing_exchange_req(data, len);
            break;
        case em_msg_type_sensing_exchange_rsp:
            handle_sensing_exchange_rsp(data, len);
            break;
        case em_msg_type_layer3_path_setup_req:
            handle_layer3_path_setup_req(data, len);
            break;
        case em_msg_type_layer3_path_setup_rsp:
            handle_layer3_path_setup_rsp(data, len);
            break;
        case em_msg_type_agent_sta_iface_config_req:
            handle_agent_sta_iface_config_req(data, len);
            break;
        case em_msg_type_agent_sta_iface_config_rprt:
            handle_agent_sta_iface_config_rprt(data, len);
            break;
        case em_msg_type_sensing_mq_req:
            handle_sensing_mq_req(data, len);
            break;
        case em_msg_type_sensing_mq_rsp:
            handle_sensing_mq_rsp(data, len);
            break;
        case em_msg_type_trigger_probe_req:
            handle_trigger_probe_req(data, len);
            break;
        case em_msg_type_trigger_probe_req_rsp:
            handle_trigger_probe_req_rsp(data, len);
            break;
        case em_msg_type_tunneled:
            handle_tunneled_probe_response(data, len);
            break;
        default:
            break;
    }
}

void em_sensing_t::handle_tunneled_probe_response(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) {
        return;
    }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    uint8_t protocol = 0U;
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) {
            return;
        }
        if (tlv->type == em_tlv_type_tunneled_msg_type && value_length >= sizeof(em_tunneled_msg_type_t)) {
            protocol = tlv->value[0];
        } else if (tlv->type == em_tlv_type_tunneled && protocol == EM_TUNNELED_PROTOCOL_SENSING_PROBE_RESPONSE) {
            if (value_length < sizeof(uint16_t)) {
                return;
            }
            uint16_t body_length = 0U;
            std::memcpy(&body_length, tlv->value, sizeof(body_length));
            body_length = ntohs(body_length);
            if (body_length > value_length - sizeof(body_length)) {
                return;
            }
            m_last_tunneled_probe_response.assign(tlv->value + sizeof(body_length),
                tlv->value + sizeof(body_length) + body_length);
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) +
            sizeof(em_tlv_t) + value_length);
    }
    (void)send_ack(data, len);
}

void em_sensing_t::process_agent_state()
{
    process_qos_null_timers();
    process_tb_timeouts_at(std::chrono::steady_clock::now());
    process_sensing_mq_timers_at(std::chrono::steady_clock::now());
}

void em_sensing_t::process_ctrl_state()
{
}

bool em_sensing_t::send_ack(unsigned char *data, unsigned int len)
{
    return send_message(data, len, em_msg_type_1905_ack, em_tlv_type_eom, nullptr, 0U);
}

bool em_sensing_t::send_message(unsigned char *request, unsigned int request_len,
    em_msg_type_t message_type, em_tlv_type_t tlv_type, const unsigned char *value,
    unsigned short value_length)
{
    if (m_send_callback == nullptr || request == nullptr ||
        request_len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) || value_length > 1024U) {
        return false;
    }
    unsigned char buffer[1200] = {0};
    const auto *request_header = reinterpret_cast<const em_raw_hdr_t *>(request);
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, request_header->src, sizeof(mac_address_t));
    std::memcpy(header->src, request_header->dst, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(message_type);
    const auto *request_cmdu = reinterpret_cast<const em_cmdu_t *>(request + sizeof(em_raw_hdr_t));
    cmdu->id = request_cmdu->id;
    cmdu->last_frag_ind = 1U;
    auto *tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    tlv->type = tlv_type;
    tlv->len = htons(value_length);
    if (value_length > 0U && value != nullptr) {
        std::memcpy(tlv->value, value, value_length);
    }
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int message_length = static_cast<unsigned int>(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) +
        sizeof(em_tlv_t) + value_length + sizeof(em_tlv_t));
    return m_send_callback(buffer, message_length) >= 0;
}

bool em_sensing_t::send_layer3_path_setup(const em_raw_hdr_t &route,
    const dm_layer3_path_info_t &path, bool add_path)
{
    if (m_send_callback == nullptr || path.destination_port == 0U ||
        path.transport_protocol > em_layer3_transport_tcp_ipv4) {
        return false;
    }
    unsigned char buffer[1200] = {0};
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, route.dst, sizeof(mac_address_t));
    std::memcpy(header->src, route.src, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_layer3_path_setup_req);
    cmdu->id = htons(1U);
    cmdu->last_frag_ind = 1U;
    em_layer3_path_setup_req_t request{};
    request.flags = add_path ? 0x80U : 0U;
    request.service_name = path.service_name;
    request.transport_protocol = path.transport_protocol;
    std::memcpy(request.destination_address, path.destination_address, 16U);
    request.destination_port = path.destination_port;
    std::vector<uint8_t> encoded;
    if (!em_encode_layer3_path_setup_req_tlv(request, encoded)) {
        return false;
    }
    auto *tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    std::memcpy(tlv, encoded.data(), encoded.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + encoded.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int length = static_cast<unsigned int>(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) +
        encoded.size() + sizeof(em_tlv_t));
    return m_send_callback(buffer, length) >= 0;
}

bool em_sensing_t::send_agent_sta_iface_config(const em_raw_hdr_t &route,
    const mac_address_t ruid, uint8_t count)
{
    if (m_send_callback == nullptr || ruid == nullptr || count > EM_MAX_RADIO_PER_AGENT) { return false; }
    em_agent_sta_iface_radio_view_t radio;
    std::memcpy(radio.ruid, ruid, sizeof(mac_address_t));
    for (uint8_t index = 0U; index < count; ++index) {
        radio.agent_sta_mac_addresses.push_back({0U, 0U, 0U, 0U, 0U, 0U});
    }
    std::vector<em_agent_sta_iface_radio_view_t> radios = {radio};
    std::vector<uint8_t> encoded;
    if (!em_encode_agent_sta_iface_tlv(radios, encoded)) { return false; }
    unsigned char buffer[1200] = {0};
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, route.dst, sizeof(mac_address_t));
    std::memcpy(header->src, route.src, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_agent_sta_iface_config_req);
    cmdu->id = htons(1U);
    cmdu->last_frag_ind = 1U;
    auto *tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    std::memcpy(tlv, encoded.data(), encoded.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + encoded.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int length = static_cast<unsigned int>(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + encoded.size() + sizeof(em_tlv_t));
    return m_send_callback(buffer, length) >= 0;
}

bool em_sensing_t::send_sensing_exchange(const em_raw_hdr_t &route,
    const em_sensing_exchange_req_t &request)
{
    if (m_send_callback == nullptr || request.exchange_id == 0U ||
        request.exchange_type > em_sensing_exchange_non_tb ||
        ((request.flags & 0x80U) != 0U && !supports_sensing_exchange(
            request.transmitter, request.exchange_type, request.data_type))) {
        return false;
    }
    if ((request.flags & 0x80U) == 0U && !m_exchange_manager.contains(request.exchange_id)) {
        return false;
    }
    if ((request.flags & 0x80U) != 0U) {
        dm_sensing_exchange_info_t exchange;
        exchange.exchange_id = request.exchange_id;
        exchange.exchange_type = request.exchange_type;
        exchange.add_exchange = true;
        exchange.measurements_requested = (request.flags & 0x40U) != 0U;
        exchange.period = request.period;
        exchange.bandwidth = request.bandwidth;
        exchange.n_tx = request.n_tx;
        exchange.n_rx = request.n_rx;
        exchange.data_type = request.data_type;
        std::memcpy(exchange.transmitter, request.transmitter, sizeof(mac_address_t));
        std::memcpy(exchange.receiver, request.receiver, sizeof(mac_address_t));
        if (!m_exchange_manager.add(exchange)) { return false; }
    }
    unsigned char buffer[1200] = {0};
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, route.dst, sizeof(mac_address_t));
    std::memcpy(header->src, route.src, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_sensing_exchange_req);
    cmdu->id = htons(1U);
    cmdu->last_frag_ind = 1U;
    std::vector<uint8_t> encoded;
    if (!em_encode_sensing_exchange_req_tlv(request, encoded)) { return false; }
    auto *tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    std::memcpy(tlv, encoded.data(), encoded.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + encoded.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int length = static_cast<unsigned int>(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + encoded.size() + sizeof(em_tlv_t));
    return m_send_callback(buffer, length) >= 0;
}

bool em_sensing_t::send_sensing_mq(const em_raw_hdr_t &route, const em_sensing_mq_req_t &request)
{
    if (m_send_callback == nullptr) {
        return false;
    }
    bool valid_sta_capability = false;
    for (const auto &radio : m_peer_capabilities.radios) {
        if ((radio.sta_flags & 0x20U) != 0U) {
            valid_sta_capability = true;
            break;
        }
    }
    if (!valid_sta_capability) {
        return false;
    }
    std::vector<uint8_t> encoded;
    if (!em_encode_sensing_mq_req_tlv(request, encoded)) {
        return false;
    }
    unsigned char buffer[1200] = {0};
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, route.dst, sizeof(mac_address_t));
    std::memcpy(header->src, route.src, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_sensing_mq_req);
    cmdu->id = htons(1U);
    cmdu->last_frag_ind = 1U;
    auto *tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    std::memcpy(tlv, encoded.data(), encoded.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + encoded.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int length = static_cast<unsigned int>(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) +
        encoded.size() + sizeof(em_tlv_t));
    return m_send_callback(buffer, length) >= 0;
}

bool em_sensing_t::send_trigger_probe(const em_raw_hdr_t &route, const em_trigger_probe_req_t &request,
    const std::vector<std::array<uint8_t, 6>> &bssids)
{
    if (m_send_callback == nullptr || bssids.size() > UINT8_MAX) {
        return false;
    }
    std::vector<uint8_t> encoded;
    if (!em_encode_trigger_probe_req_tlv(request, bssids, encoded)) {
        return false;
    }
    unsigned char buffer[1200] = {0};
    auto *header = reinterpret_cast<em_raw_hdr_t *>(buffer);
    std::memcpy(header->dst, route.dst, sizeof(mac_address_t));
    std::memcpy(header->src, route.src, sizeof(mac_address_t));
    header->type = htons(ETH_P_1905);
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(buffer + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_trigger_probe_req);
    cmdu->id = htons(1U);
    cmdu->last_frag_ind = 1U;
    auto *tlv = reinterpret_cast<em_tlv_t *>(buffer + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    std::memcpy(tlv, encoded.data(), encoded.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + encoded.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    const unsigned int length = static_cast<unsigned int>(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) +
        encoded.size() + sizeof(em_tlv_t));
    return m_send_callback(buffer, length) >= 0;
}

bool em_sensing_t::validate_agent_sta_iface_request(const mac_address_t ruid, uint8_t count) const
{
    if (ruid == nullptr || count > EM_MAX_RADIO_PER_AGENT) { return false; }
    em_sensing_capability_snapshot_t capabilities;
    if (!get_sensing_capabilities(capabilities)) { return false; }
    for (const auto &radio : capabilities.radios) {
        if (std::memcmp(radio.ruid, ruid, sizeof(mac_address_t)) == 0) {
            return count <= EM_MAX_RADIO_PER_AGENT;
        }
    }
    return false;
}

unsigned short em_sensing_t::create_agent_sta_interface_report_tlv(unsigned char *buffer,
    const mac_address_t ruid, const std::vector<std::array<uint8_t, 6>> &addresses) const
{
    if (buffer == nullptr || ruid == nullptr || addresses.size() > EM_MAX_RADIO_PER_AGENT) { return 0U; }
    em_agent_sta_iface_radio_view_t radio;
    std::memcpy(radio.ruid, ruid, sizeof(mac_address_t));
    radio.agent_sta_mac_addresses = addresses;
    std::vector<em_agent_sta_iface_radio_view_t> radios = {radio};
    std::vector<uint8_t> encoded;
    if (!em_encode_agent_sta_iface_tlv(radios, encoded)) { return 0U; }
    std::memcpy(buffer, encoded.data() + sizeof(em_tlv_t), encoded.size() - sizeof(em_tlv_t));
    return static_cast<unsigned short>(encoded.size() - sizeof(em_tlv_t));
}

unsigned short em_sensing_t::create_agent_sta_interface_topology_tlv(unsigned char *buffer,
    const mac_address_t ruid, const std::vector<std::array<uint8_t, 6>> &addresses) const
{
    return create_agent_sta_interface_report_tlv(buffer, ruid, addresses);
}

#define EM_SENSING_EMPTY_HANDLER(name) \
    void em_sensing_t::name(unsigned char *data, unsigned int len) \
    { \
        (void)data; \
        (void)len; \
    }

void em_sensing_t::handle_sensing_exchange_req(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) { return; }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) { return; }
        if (tlv->type == em_tlv_type_sensing_exchange_req) {
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            em_sensing_exchange_req_t request{};
            if (!em_decode_sensing_exchange_req_tlv(encoded.data(), encoded.size(), request)) { return; }
            em_sensing_exchange_rsp_t response{};
            response.exchange_id = request.exchange_id;
            (void)send_ack(data, len);
            bool accepted = false;
            bool defer_response = false;
            if ((request.flags & 0x80U) == 0U) {
                dm_sensing_exchange_info_t exchange{};
                if (m_exchange_manager.get(request.exchange_id, exchange) &&
                    exchange.exchange_type == em_sensing_exchange_qos_null) {
                    stop_qos_null_timer(request.exchange_id);
                    accepted = m_lower_layer->stop_qos_null_exchange(request.exchange_id);
                } else {
                    accepted = m_lower_layer->send_sensing_measurement_termination(request.exchange_id);
                    stop_qos_null_timer(request.exchange_id);
                }
                clear_tb_state(request.exchange_id);
                response.result_code = accepted ? em_sensing_exchange_terminated : em_sensing_exchange_request_declined;
            } else if ((request.flags & 0x40U) != 0U && m_path_manager.size() == 0U) {
                response.result_code = em_sensing_exchange_no_layer3_path;
            } else if (m_exchange_manager.contains(request.exchange_id) ||
                m_exchange_manager.size() >= em_sensing_exchange_manager_t::max_exchanges) {
                response.result_code = em_sensing_exchange_device_unavailable;
            } else if (request.exchange_type == em_sensing_exchange_qos_null) {
                if (m_local_bss_callback == nullptr || !m_local_bss_callback(request.receiver) ||
                    m_associated_sta_callback == nullptr ||
                    !m_associated_sta_callback(request.receiver, request.transmitter)) {
                    response.result_code = em_sensing_exchange_device_unavailable;
                } else {
                    accepted = m_lower_layer->start_qos_null_exchange(request.exchange_id);
                    response.result_code = accepted ? em_sensing_exchange_created : em_sensing_exchange_device_unavailable;
                }
            } else if (request.exchange_type == em_sensing_exchange_tb) {
                uint8_t tb_result = em_sensing_exchange_device_unavailable;
                accepted = start_tb_exchange(request, data, len, tb_result);
                response.result_code = tb_result;
                // The outcome is reported from the Sensing Measurement Response or the query timeout.
                defer_response = accepted;
            } else if (request.exchange_type == em_sensing_exchange_non_tb) {
                uint8_t non_tb_result = em_sensing_exchange_device_unavailable;
                accepted = start_tb_exchange(request, data, len, non_tb_result, true);
                response.result_code = non_tb_result;
                defer_response = accepted;
            } else {
                response.result_code = em_sensing_exchange_request_declined;
            }
            if (accepted && (request.flags & 0x80U) == 0U) {
                (void)m_exchange_manager.remove(request.exchange_id, em_sensing_exchange_terminated);
                (void)m_session_manager.notify_exchange_terminated(request.exchange_id,
                    em_sensing_exchange_terminated);
            } else if (accepted && request.exchange_type != em_sensing_exchange_tb) {
                dm_sensing_exchange_info_t exchange;
                exchange.exchange_id = request.exchange_id;
                exchange.exchange_type = request.exchange_type;
                exchange.add_exchange = true;
                exchange.measurements_requested = (request.flags & 0x40U) != 0U;
                exchange.period = request.period;
                exchange.bandwidth = request.bandwidth;
                exchange.data_type = request.data_type;
                std::memcpy(exchange.transmitter, request.transmitter, sizeof(mac_address_t));
                std::memcpy(exchange.receiver, request.receiver, sizeof(mac_address_t));
                (void)m_exchange_manager.add(exchange);
                if (request.exchange_type == em_sensing_exchange_qos_null) {
                    m_qos_null_request_frames[request.exchange_id] = std::vector<uint8_t>(data, data + len);
                    arm_qos_null_timer(request.exchange_id, request.period);
                }
            }
            std::vector<uint8_t> response_tlv;
            if (!defer_response && em_encode_sensing_exchange_rsp_tlv(response, response_tlv)) {
                (void)send_message(data, len, em_msg_type_sensing_exchange_rsp, em_tlv_type_sensing_exchange_rsp,
                    response_tlv.data() + sizeof(em_tlv_t), static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
            }
            return;
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    }
}
void em_sensing_t::handle_sensing_exchange_rsp(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) { return; }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) { return; }
        if (tlv->type == em_tlv_type_sensing_exchange_rsp) {
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            em_sensing_exchange_rsp_t response{};
            if (!em_decode_sensing_exchange_rsp_tlv(encoded.data(), encoded.size(), response)) { return; }
            (void)send_ack(data, len);
            if (response.result_code == em_sensing_exchange_terminated ||
                response.result_code == em_sensing_exchange_timeout ||
                response.result_code == em_sensing_exchange_request_declined) {
                m_exchange_manager.remove(response.exchange_id, response.result_code);
                (void)m_session_manager.notify_exchange_terminated(response.exchange_id, response.result_code);
            } else {
                (void)m_exchange_manager.update_result(response.exchange_id, response.result_code);
            }
            return;
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    }
}
void em_sensing_t::handle_layer3_path_setup_req(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) {
        return;
    }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) {
            return;
        }
        if (tlv->type == em_tlv_type_layer3_path_setup_req) {
            em_layer3_path_setup_req_t request{};
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            if (!em_decode_layer3_path_setup_req_tlv(encoded.data(), encoded.size(), request)) {
                return;
            }
            dm_layer3_path_info_t result{};
            const bool added = (request.flags & 0x80U) != 0U ?
                m_path_manager.add_path(request, result) : m_path_manager.remove_path(request, result);
            (void)send_ack(data, len);
            em_layer3_path_setup_rsp_t response{};
            response.service_name = result.service_name;
            response.result_code = added ? 0U : 1U;
            response.source_port = result.source_port;
            std::vector<uint8_t> response_tlv;
            if (em_encode_layer3_path_setup_rsp_tlv(response, response_tlv)) {
                (void)send_message(data, len, em_msg_type_layer3_path_setup_rsp,
                    em_tlv_type_layer3_path_setup_rsp,
                    response_tlv.data() + sizeof(em_tlv_t),
                    static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
            }
            return;
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    }
}
void em_sensing_t::handle_layer3_path_setup_rsp(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) { return; }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) { return; }
        if (tlv->type == em_tlv_type_layer3_path_setup_rsp) {
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            em_layer3_path_setup_rsp_t response{};
            if (!em_decode_layer3_path_setup_rsp_tlv(encoded.data(), encoded.size(), response)) { return; }
            (void)send_ack(data, len);
            dm_layer3_path_info_t path;
            path.service_name = response.service_name;
            path.source_port = response.source_port;
            path.active = response.result_code == 0U;
            if (m_path_result_callback != nullptr) { m_path_result_callback(path); }
            return;
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    }
}
void em_sensing_t::handle_agent_sta_iface_config_req(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) {
        return;
    }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) {
            return;
        }
        if (tlv->type == em_tlv_type_agent_sta_iface) {
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            std::vector<em_agent_sta_iface_radio_view_t> radios;
            if (!em_decode_agent_sta_iface_tlv(encoded.data(), encoded.size(), radios)) {
                return;
            }
            (void)send_ack(data, len);
            std::vector<em_agent_sta_iface_radio_view_t> report;
            for (const auto &radio : radios) {
                if (m_lower_layer == nullptr) {
                    return;
                }
                if (!validate_agent_sta_iface_request(radio.ruid,
                    static_cast<uint8_t>(radio.agent_sta_mac_addresses.size()))) {
                    return;
                }
                if (radio.agent_sta_mac_addresses.empty()) {
                    const auto interface = std::find_if(m_agent_sta_interfaces.begin(),
                        m_agent_sta_interfaces.end(), [&radio](const auto &entry) {
                            return std::memcmp(entry.ruid, radio.ruid, sizeof(mac_address_t)) == 0;
                        });
                    if (interface != m_agent_sta_interfaces.end()) {
                        std::vector<dm_sensing_exchange_info_t> terminated;
                        m_exchange_manager.remove_matching([&interface](const auto &exchange) {
                            return std::any_of(interface->agent_sta_mac_addresses.begin(),
                                interface->agent_sta_mac_addresses.end(), [&exchange](const auto &address) {
                                    return std::memcmp(exchange.transmitter, address.data(), sizeof(mac_address_t)) == 0 ||
                                        std::memcmp(exchange.receiver, address.data(), sizeof(mac_address_t)) == 0;
                                });
                        }, em_sensing_exchange_terminated, terminated);
                        for (const auto &exchange : terminated) {
                            (void)m_lower_layer->send_sensing_measurement_termination(exchange.exchange_id);
                            (void)m_session_manager.notify_exchange_terminated(exchange.exchange_id,
                                em_sensing_exchange_terminated);
                            em_sensing_exchange_rsp_t response{};
                            response.exchange_id = exchange.exchange_id;
                            response.result_code = em_sensing_exchange_terminated;
                            std::vector<uint8_t> response_tlv;
                            if (em_encode_sensing_exchange_rsp_tlv(response, response_tlv)) {
                                (void)send_message(data, len, em_msg_type_sensing_exchange_rsp,
                                    em_tlv_type_sensing_exchange_rsp,
                                    response_tlv.data() + sizeof(em_tlv_t),
                                    static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
                            }
                        }
                        m_agent_sta_interfaces.erase(interface);
                    }
                    (void)m_lower_layer->destroy_agent_sta_iface(radio.ruid);
                } else {
                    std::vector<std::array<uint8_t, 6>> addresses;
                    if (m_lower_layer->create_agent_sta_iface(radio.ruid,
                        static_cast<uint8_t>(radio.agent_sta_mac_addresses.size()), addresses)) {
                        em_agent_sta_iface_radio_view_t result;
                        std::memcpy(result.ruid, radio.ruid, sizeof(mac_address_t));
                        result.agent_sta_mac_addresses = addresses;
                        report.push_back(result);
                        auto interface = std::find_if(m_agent_sta_interfaces.begin(),
                            m_agent_sta_interfaces.end(), [&radio](const auto &entry) {
                                return std::memcmp(entry.ruid, radio.ruid, sizeof(mac_address_t)) == 0;
                            });
                        if (interface == m_agent_sta_interfaces.end()) {
                            m_agent_sta_interfaces.push_back(result);
                        } else {
                            *interface = result;
                        }
                    }
                }
            }
            std::vector<uint8_t> response_tlv;
            if (em_encode_agent_sta_iface_tlv(report, response_tlv)) {
                (void)send_message(data, len, em_msg_type_agent_sta_iface_config_rprt,
                    em_tlv_type_agent_sta_iface,
                    response_tlv.data() + sizeof(em_tlv_t),
                    static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
            }
            return;
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    }
}
void em_sensing_t::handle_agent_sta_iface_config_rprt(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) { return; }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) { return; }
        if (tlv->type == em_tlv_type_agent_sta_iface) {
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            std::vector<em_agent_sta_iface_radio_view_t> report;
            if (!em_decode_agent_sta_iface_tlv(encoded.data(), encoded.size(), report)) { return; }
            (void)send_ack(data, len);
            if (m_agent_sta_report_callback != nullptr) { m_agent_sta_report_callback(report); }
            return;
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) + sizeof(em_tlv_t) + value_length);
    }
}
void em_sensing_t::handle_sensing_mq_req(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + sizeof(em_tlv_t)) {
        return;
    }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    const size_t value_length = ntohs(tlv->len);
    if (tlv->type != em_tlv_type_sensing_mq_req ||
        len < offset + sizeof(em_tlv_t) + value_length) {
        return;
    }
    std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
    std::memcpy(encoded.data(), tlv, encoded.size());
    em_sensing_mq_req_t request{};
    if (!em_decode_sensing_mq_req_tlv(encoded.data(), encoded.size(), request)) {
        return;
    }
    (void)send_ack(data, len);
    em_sensing_mq_rsp_t response{};
    std::memcpy(response.agent_sta_mac_addr, request.agent_sta_mac_addr, sizeof(mac_address_t));
    std::memcpy(response.bssid, request.bssid, sizeof(mac_address_t));
    bool known = false;
    for (const auto &radio : m_agent_sta_interfaces) {
        for (const auto &address : radio.agent_sta_mac_addresses) {
            if (std::memcmp(address.data(), request.agent_sta_mac_addr, sizeof(mac_address_t)) == 0) {
                known = true;
                break;
            }
        }
    }
    if (!known) {
        response.result_code = em_sensing_mq_sta_not_present;
    } else if (m_lower_layer == nullptr) {
        response.result_code = em_sensing_mq_sta_unavailable;
    } else {
        const bool sent = m_lower_layer->send_sensing_measurement_query(
            request.agent_sta_mac_addr, request.bssid);
        if (!sent) {
            response.result_code = em_sensing_mq_sta_unavailable;
        } else {
            sensing_mq_pending_t pending;
            pending.request = request;
            pending.route_frame.assign(data, data + len);
            pending.response_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            pending.expiry_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            m_sensing_mq_pending[sensing_mq_key(request.agent_sta_mac_addr, request.bssid)] = pending;
            return;
        }
    }
    std::vector<uint8_t> response_tlv;
    if (em_encode_sensing_mq_rsp_tlv(response, response_tlv)) {
        (void)send_message(data, len, em_msg_type_sensing_mq_rsp, em_tlv_type_sensing_mq_rsp,
            response_tlv.data() + sizeof(em_tlv_t),
            static_cast<unsigned short>(response_tlv.size() - sizeof(em_tlv_t)));
    }
}

void em_sensing_t::handle_sensing_mq_rsp(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + sizeof(em_tlv_t)) {
        return;
    }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    const size_t value_length = ntohs(tlv->len);
    if (tlv->type != em_tlv_type_sensing_mq_rsp ||
        len < offset + sizeof(em_tlv_t) + value_length) {
        return;
    }
    std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
    std::memcpy(encoded.data(), tlv, encoded.size());
    em_sensing_mq_rsp_t response{};
    if (!em_decode_sensing_mq_rsp_tlv(encoded.data(), encoded.size(), response)) {
        return;
    }
    (void)send_ack(data, len);
    m_sensing_mq_results[sensing_mq_key(response.agent_sta_mac_addr, response.bssid)] = response.result_code;
}
void em_sensing_t::handle_trigger_probe_req(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + sizeof(em_tlv_t)) {
        return;
    }
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    const size_t value_length = ntohs(tlv->len);
    if (tlv->type != em_tlv_type_trigger_probe_req ||
        len < offset + sizeof(em_tlv_t) + value_length) {
        return;
    }
    std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
    std::memcpy(encoded.data(), tlv, encoded.size());
    em_trigger_probe_req_t request{};
    std::vector<std::array<uint8_t, 6>> bssids;
    if (!em_decode_trigger_probe_req_tlv(encoded.data(), encoded.size(), request, bssids)) {
        return;
    }
    bool known = false;
    for (const auto &radio : m_agent_sta_interfaces) {
        for (const auto &address : radio.agent_sta_mac_addresses) {
            if (std::memcmp(address.data(), request.agent_sta_mac_addr, sizeof(mac_address_t)) == 0) {
                known = true;
                break;
            }
        }
    }
    if (!known) {
        em_error_code_t error{};
        error.reason_code = 0x07U;
        std::memcpy(error.sta_mac_addr, request.agent_sta_mac_addr, sizeof(mac_address_t));
        (void)send_message(data, len, em_msg_type_1905_ack, em_tlv_type_error_code,
            reinterpret_cast<const unsigned char *>(&error), sizeof(error));
        return;
    }
    (void)send_ack(data, len);
    if (m_lower_layer != nullptr && !m_lower_layer->send_probe_request(request.agent_sta_mac_addr, bssids)) {
        send_trigger_probe_failure(data, len, request, bssids);
    }
}
void em_sensing_t::handle_trigger_probe_req_rsp(unsigned char *data, unsigned int len)
{
    if (data == nullptr || len < sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t)) {
        return;
    }
    (void)send_ack(data, len);
    const size_t offset = sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t);
    size_t remaining = len - offset;
    auto *tlv = reinterpret_cast<em_tlv_t *>(data + offset);
    m_last_trigger_probe_failures.clear();
    while (remaining >= sizeof(em_tlv_t) && tlv->type != em_tlv_type_eom) {
        const size_t value_length = ntohs(tlv->len);
        if (remaining < sizeof(em_tlv_t) + value_length) {
            return;
        }
        if (tlv->type == em_tlv_type_status_code && value_length >= sizeof(em_status_code_t)) {
            const auto *status = reinterpret_cast<const em_status_code_t *>(tlv->value);
            m_last_trigger_probe_status = ntohs(status->status_code);
        } else if (tlv->type == em_tlv_type_trigger_probe_req) {
            std::vector<uint8_t> encoded(sizeof(em_tlv_t) + value_length);
            std::memcpy(encoded.data(), tlv, encoded.size());
            em_trigger_probe_req_t request{};
            if (!em_decode_trigger_probe_req_tlv(encoded.data(), encoded.size(), request,
                m_last_trigger_probe_failures)) {
                return;
            }
        }
        remaining -= sizeof(em_tlv_t) + value_length;
        tlv = reinterpret_cast<em_tlv_t *>(reinterpret_cast<unsigned char *>(tlv) +
            sizeof(em_tlv_t) + value_length);
    }
}

#undef EM_SENSING_EMPTY_HANDLER
