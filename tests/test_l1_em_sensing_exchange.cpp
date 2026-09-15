#include <gtest/gtest.h>

#include "em_sensing_exchange.h"
#include "em_sensing.h"
#include "em_sensing_tlv.h"

#include <array>
#include <cstring>
#include <memory>
#include <vector>

namespace {

std::vector<uint8_t> make_exchange_request(const em_sensing_exchange_req_t &request)
{
    std::vector<uint8_t> tlv;
    EXPECT_TRUE(em_encode_sensing_exchange_req_tlv(request, tlv));
    std::vector<uint8_t> frame(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + tlv.size() + sizeof(em_tlv_t));
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(frame.data() + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_sensing_exchange_req);
    std::memcpy(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t), tlv.data(), tlv.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + tlv.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    return frame;
}

uint8_t response_result(const std::vector<uint8_t> &frame)
{
    const auto *tlv = reinterpret_cast<const em_tlv_t *>(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    em_sensing_exchange_rsp_t response{};
    EXPECT_TRUE(em_decode_sensing_exchange_rsp_tlv(reinterpret_cast<const uint8_t *>(tlv),
        sizeof(em_tlv_t) + ntohs(tlv->len), response));
    return response.result_code;
}

class qos_null_test_ll_t final : public em_sensing_ll_t {
public:
    bool awake = true;
    bool emit_measurement = true;
    bool tb_accept = true;
    bool emit_tb_response = true;
    uint16_t tb_status_code = EM_SENSING_STATUS_SUCCESS;
    unsigned int frame_count = 0U;
    unsigned int queued_frame_count = 0U;
    unsigned int stop_count = 0U;
    unsigned int tb_request_count = 0U;
    unsigned int mq_query_count = 0U;
    unsigned int probe_request_count = 0U;
    bool probe_request_accept = true;
    std::vector<std::array<uint8_t, 6>> last_probe_bssids;
    em_sensing_measurement_params_t last_tb_params;
    em_sensing_event_callback_t callback;

    bool get_capabilities(em_sensing_capability_snapshot_t &capabilities) const override
    {
        capabilities = {};
        em_sensing_radio_capability_t radio;
        radio.bss_flags = 0xe0U;
        radio.sta_flags = 0xe0U;
        capabilities.radios.push_back(radio);
        return true;
    }
    bool send_sensing_measurement_request(uint32_t) override { return true; }
    bool send_sensing_measurement_request_frame(uint32_t exchange_id, const mac_address_t,
        const mac_address_t, const em_sensing_measurement_params_t &params) override
    {
        if (!tb_accept) { return false; }
        ++tb_request_count;
        last_tb_params = params;
        if (emit_tb_response && callback) {
            em_sensing_measurement_event_t event;
            event.event_type = em_sensing_measurement_event_t::measurement_response;
            event.exchange_id = exchange_id;
            event.status_code = tb_status_code;
            callback(event);
        }
        return true;
    }
    bool send_sensing_measurement_query(const mac_address_t, const mac_address_t) override
    {
        ++mq_query_count;
        return true;
    }
    bool send_sensing_measurement_termination(uint32_t) override { return true; }
    bool start_qos_null_exchange(uint32_t) override { return true; }
    bool stop_qos_null_exchange(uint32_t) override { ++stop_count; return true; }
    bool sta_is_awake(const mac_address_t) const override { return awake; }
    bool send_qos_null_frame(uint32_t exchange_id, const mac_address_t bssid,
        const mac_address_t sta_mac, bool queue_for_power_save) override
    {
        ++frame_count;
        if (queue_for_power_save) { ++queued_frame_count; }
        if (emit_measurement && callback) {
            em_sensing_measurement_event_t event;
            event.exchange_id = exchange_id;
            std::memcpy(event.transmitter, sta_mac, sizeof(mac_address_t));
            std::memcpy(event.receiver, bssid, sizeof(mac_address_t));
            event.data = {0U};
            callback(event);
        }
        return true;
    }
    bool create_agent_sta_iface(const mac_address_t ruid, uint8_t count,
        std::vector<std::array<uint8_t, 6>> &addresses) override
    {
        addresses.clear();
        for (uint8_t index = 0U; index < count; ++index) {
            std::array<uint8_t, 6> address{};
            std::memcpy(address.data(), ruid, sizeof(mac_address_t));
            address[5] = static_cast<uint8_t>(address[5] + index + 1U);
            addresses.push_back(address);
        }
        return true;
    }
    bool destroy_agent_sta_iface(const mac_address_t) override { return true; }
    bool send_probe_request(const mac_address_t, const std::vector<std::array<uint8_t, 6>> &bssids) override
    {
        ++probe_request_count;
        last_probe_bssids = bssids;
        return probe_request_accept;
    }
    bool agent_sta_is_unassociated_only() const override { return true; }
    void set_event_callback(em_sensing_event_callback_t value) override { callback = std::move(value); }
};

std::vector<uint8_t> create_valid_qos_null_request(uint32_t exchange_id, uint16_t period)
{
    em_sensing_exchange_req_t request{};
    request.exchange_id = exchange_id;
    request.exchange_type = em_sensing_exchange_qos_null;
    request.flags = 0x80U;
    request.period = period;
    return make_exchange_request(request);
}

std::vector<uint8_t> create_tb_request(uint32_t exchange_id, bool measurements_requested = false)
{
    em_sensing_exchange_req_t request{};
    request.exchange_id = exchange_id;
    request.exchange_type = em_sensing_exchange_tb;
    request.flags = static_cast<unsigned char>(0x80U | (measurements_requested ? 0x40U : 0x00U));
    request.period = 7U;
    request.bandwidth = 80U;
    request.n_tx = 2U;
    request.n_rx = 3U;
    const mac_address_t transmitter = {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x01U};
    const mac_address_t receiver = {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U};
    std::memcpy(request.transmitter, transmitter, sizeof(mac_address_t));
    std::memcpy(request.receiver, receiver, sizeof(mac_address_t));
    return make_exchange_request(request);
}

std::vector<uint8_t> create_non_tb_request(uint32_t exchange_id, const mac_address_t transmitter,
    const mac_address_t receiver)
{
    em_sensing_exchange_req_t request{};
    request.exchange_id = exchange_id;
    request.exchange_type = em_sensing_exchange_non_tb;
    request.flags = 0x80U;
    request.bandwidth = 40U;
    request.n_tx = 2U;
    request.n_rx = 1U;
    std::memcpy(request.transmitter, transmitter, sizeof(mac_address_t));
    std::memcpy(request.receiver, receiver, sizeof(mac_address_t));
    return make_exchange_request(request);
}

std::vector<uint8_t> create_mq_request(const mac_address_t agent_sta, const mac_address_t bssid)
{
    em_sensing_mq_req_t request{};
    std::memcpy(request.agent_sta_mac_addr, agent_sta, sizeof(mac_address_t));
    std::memcpy(request.bssid, bssid, sizeof(mac_address_t));
    std::vector<uint8_t> tlv;
    EXPECT_TRUE(em_encode_sensing_mq_req_tlv(request, tlv));
    std::vector<uint8_t> frame(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + tlv.size() + sizeof(em_tlv_t));
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(frame.data() + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_sensing_mq_req);
    std::memcpy(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t), tlv.data(), tlv.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + tlv.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    return frame;
}

std::vector<uint8_t> create_trigger_probe_request(const mac_address_t agent_sta,
    const std::vector<std::array<uint8_t, 6>> &bssids)
{
    em_trigger_probe_req_t request{};
    std::memcpy(request.agent_sta_mac_addr, agent_sta, sizeof(mac_address_t));
    std::vector<uint8_t> tlv;
    EXPECT_TRUE(em_encode_trigger_probe_req_tlv(request, bssids, tlv));
    std::vector<uint8_t> frame(sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + tlv.size() + sizeof(em_tlv_t));
    auto *cmdu = reinterpret_cast<em_cmdu_t *>(frame.data() + sizeof(em_raw_hdr_t));
    cmdu->type = htons(em_msg_type_trigger_probe_req);
    cmdu->id = htons(1U);
    std::memcpy(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t), tlv.data(), tlv.size());
    auto *eom = reinterpret_cast<em_tlv_t *>(frame.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t) + tlv.size());
    eom->type = em_tlv_type_eom;
    eom->len = 0U;
    return frame;
}

} // namespace

TEST(em_sensing_exchange, tracks_unique_exchange_ids_and_results)
{
    em_sensing_exchange_manager_t manager;
    dm_sensing_exchange_info_t exchange;
    exchange.exchange_id = 42U;
    exchange.exchange_type = em_sensing_exchange_tb;
    ASSERT_TRUE(manager.add(exchange));
    EXPECT_FALSE(manager.add(exchange));
    EXPECT_TRUE(manager.contains(42U));
    EXPECT_TRUE(manager.update_result(42U, em_sensing_exchange_created));
    ASSERT_TRUE(manager.get(42U, exchange));
    EXPECT_EQ(exchange.result_code, em_sensing_exchange_created);
    EXPECT_TRUE(manager.remove(42U, em_sensing_exchange_terminated));
    EXPECT_FALSE(manager.contains(42U));
}

TEST(em_sensing_exchange, rejects_reserved_and_duplicate_exchanges)
{
    em_sensing_exchange_manager_t manager;
    dm_sensing_exchange_info_t exchange;
    exchange.exchange_id = 1U;
    ASSERT_TRUE(manager.add(exchange));
    EXPECT_FALSE(manager.add(exchange));
    EXPECT_FALSE(manager.add(dm_sensing_exchange_info_t{}));
}

TEST(em_sensing_exchange, round_trips_all_result_codes)
{
    const std::vector<uint8_t> result_codes = {
        em_sensing_exchange_created,
        em_sensing_exchange_terminated,
        em_sensing_exchange_no_layer3_path,
        em_sensing_exchange_device_unavailable,
        em_sensing_exchange_request_declined,
        em_sensing_exchange_timeout,
    };

    for (const uint8_t result_code : result_codes) {
        em_sensing_exchange_rsp_t request{42U, result_code};
        std::vector<uint8_t> encoded;
        ASSERT_TRUE(em_encode_sensing_exchange_rsp_tlv(request, encoded));

        em_sensing_exchange_rsp_t decoded{};
        ASSERT_TRUE(em_decode_sensing_exchange_rsp_tlv(encoded.data(), encoded.size(), decoded));
        EXPECT_EQ(decoded.exchange_id, request.exchange_id);
        EXPECT_EQ(decoded.result_code, request.result_code);
    }
}

TEST(em_sensing_exchange, rejects_invalid_result_code_values)
{
    em_sensing_exchange_rsp_t request{42U, 7U};
    std::vector<uint8_t> encoded;
    EXPECT_FALSE(em_encode_sensing_exchange_rsp_tlv(request, encoded));

    em_sensing_exchange_rsp_t decoded{};
    const std::vector<uint8_t> invalid = {
        static_cast<uint8_t>(em_tlv_type_sensing_exchange_rsp),
        0U,
        5U,
        0U,
        0U,
        0U,
        7U,
    };
    EXPECT_FALSE(em_decode_sensing_exchange_rsp_tlv(invalid.data(), invalid.size(), decoded));
}

TEST(em_sensing_exchange, updates_manager_for_each_result_code)
{
    em_sensing_exchange_manager_t manager;
    for (uint8_t result_code = em_sensing_exchange_created;
         result_code <= em_sensing_exchange_timeout; ++result_code) {
        dm_sensing_exchange_info_t exchange{};
        exchange.exchange_id = 100U + result_code;
        exchange.exchange_type = em_sensing_exchange_qos_null;
        ASSERT_TRUE(manager.add(exchange));
        EXPECT_TRUE(manager.update_result(exchange.exchange_id, result_code));
        EXPECT_TRUE(manager.contains(exchange.exchange_id));
        dm_sensing_exchange_info_t stored{};
        ASSERT_TRUE(manager.get(exchange.exchange_id, stored));
        EXPECT_EQ(stored.result_code, result_code);
        EXPECT_TRUE(manager.remove(exchange.exchange_id, result_code));
        EXPECT_FALSE(manager.contains(exchange.exchange_id));
    }
}

TEST(em_sensing_exchange, terminates_only_exchanges_bound_to_removed_agent_sta)
{
    em_sensing_exchange_manager_t manager;
    dm_sensing_exchange_info_t dependent{};
    dependent.exchange_id = 301U;
    dependent.transmitter[5] = 1U;
    ASSERT_TRUE(manager.add(dependent));

    dm_sensing_exchange_info_t independent{};
    independent.exchange_id = 302U;
    independent.transmitter[5] = 2U;
    ASSERT_TRUE(manager.add(independent));

    std::vector<dm_sensing_exchange_info_t> removed;
    manager.remove_matching([](const dm_sensing_exchange_info_t &exchange) {
        return exchange.transmitter[5] == 1U;
    }, em_sensing_exchange_terminated, removed);

    ASSERT_EQ(removed.size(), 1U);
    EXPECT_EQ(removed.front().exchange_id, dependent.exchange_id);
    EXPECT_EQ(removed.front().result_code, em_sensing_exchange_terminated);
    EXPECT_FALSE(manager.contains(dependent.exchange_id));
    EXPECT_TRUE(manager.contains(independent.exchange_id));
}

TEST(em_sensing_exchange, rejects_qos_null_for_nonlocal_receiver_bss)
{
    em_sensing_t sensing;
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return false; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    em_sensing_exchange_req_t request{};
    request.exchange_id = 401U;
    request.exchange_type = em_sensing_exchange_qos_null;
    request.flags = 0x80U;
    request.receiver[5] = 1U;
    std::vector<uint8_t> frame = make_exchange_request(request);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_device_unavailable);
}

TEST(em_sensing_exchange, rejects_qos_null_for_unassociated_transmitter)
{
    em_sensing_t sensing;
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return false; });

    em_sensing_exchange_req_t request{};
    request.exchange_id = 402U;
    request.exchange_type = em_sensing_exchange_qos_null;
    request.flags = 0x80U;
    std::vector<uint8_t> frame = make_exchange_request(request);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_device_unavailable);
}

TEST(em_sensing_exchange, creates_qos_null_exchange_for_local_associated_sta)
{
    em_sensing_t sensing;
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    em_sensing_exchange_req_t request{};
    request.exchange_id = 403U;
    request.exchange_type = em_sensing_exchange_qos_null;
    request.flags = 0x80U;
    std::vector<uint8_t> frame = make_exchange_request(request);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_created);
    EXPECT_TRUE(sensing.has_exchange(request.exchange_id));
}

TEST(em_sensing_exchange, terminates_one_shot_qos_null_after_first_phase)
{
    em_sensing_t sensing;
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    em_sensing_exchange_req_t request{};
    request.exchange_id = 404U;
    request.exchange_type = em_sensing_exchange_qos_null;
    request.flags = 0x80U;
    request.period = 0U;
    std::vector<uint8_t> frame = make_exchange_request(request);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    ASSERT_TRUE(sensing.has_exchange(request.exchange_id));

    sensing.process_qos_null_timers();

    ASSERT_EQ(sent.size(), 3U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_terminated);
    EXPECT_FALSE(sensing.has_exchange(request.exchange_id));
}

TEST(em_sensing_exchange, queues_qos_null_for_dozing_sta_without_duplicate_phase)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    lower_layer->awake = false;
    lower_layer->emit_measurement = false;
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_valid_qos_null_request(405U, 1U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    sensing.process_qos_null_timers_at(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    sensing.process_qos_null_timers_at(std::chrono::steady_clock::now() + std::chrono::seconds(2));

    EXPECT_EQ(stub->frame_count, 1U);
    EXPECT_EQ(stub->queued_frame_count, 1U);
    EXPECT_TRUE(sensing.has_exchange(405U));
}

TEST(em_sensing_exchange, repeats_qos_null_after_measurement_completion)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_valid_qos_null_request(406U, 1U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    sensing.process_qos_null_timers_at(std::chrono::steady_clock::now() + std::chrono::seconds(1));
    sensing.process_qos_null_timers_at(std::chrono::steady_clock::now() + std::chrono::seconds(2));

    EXPECT_EQ(stub->frame_count, 2U);
    EXPECT_TRUE(sensing.has_exchange(406U));
}

TEST(em_sensing_exchange, explicitly_terminates_qos_null_exchange)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> create_frame = create_valid_qos_null_request(407U, 1U);
    sensing.process_msg(create_frame.data(), static_cast<unsigned int>(create_frame.size()));
    em_sensing_exchange_req_t terminate{};
    terminate.exchange_id = 407U;
    terminate.exchange_type = em_sensing_exchange_qos_null;
    std::vector<uint8_t> terminate_frame = make_exchange_request(terminate);
    sensing.process_msg(terminate_frame.data(), static_cast<unsigned int>(terminate_frame.size()));

    EXPECT_EQ(stub->stop_count, 1U);
    EXPECT_FALSE(sensing.has_exchange(407U));
    ASSERT_EQ(sent.size(), 4U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_terminated);
}

TEST(em_sensing_exchange, rejects_tb_exchange_without_local_bss)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return false; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_tb_request(420U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_device_unavailable);
    EXPECT_EQ(stub->tb_request_count, 0U);
    EXPECT_FALSE(sensing.has_exchange(420U));
}

TEST(em_sensing_exchange, transmits_tb_request_immediately_for_associated_peer)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    // Only the transmitter address is a locally operated BSS.
    sensing.set_local_bss_callback([](const mac_address_t mac) { return mac[5] == 0x01U; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_tb_request(421U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    EXPECT_EQ(stub->tb_request_count, 1U);
    EXPECT_FALSE(stub->last_tb_params.sensing_transmitter);
    EXPECT_TRUE(stub->last_tb_params.sensing_receiver);
    // MeasurementsRequested is clear, so the report bits stay reserved.
    EXPECT_FALSE(stub->last_tb_params.report_requested);
    EXPECT_FALSE(stub->last_tb_params.report_timestamp);
    EXPECT_EQ(stub->last_tb_params.bandwidth, 80U);
    EXPECT_EQ(stub->last_tb_params.tx_sts, 2U);
    EXPECT_EQ(stub->last_tb_params.rx_sts, 3U);
    EXPECT_EQ(stub->last_tb_params.num_rx_chains, 3U);
    EXPECT_EQ(stub->last_tb_params.rsta_availability, 7U);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_created);
}

TEST(em_sensing_exchange, waits_for_query_before_transmitting_tb_request_to_unassociated_peer)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t mac) { return mac[5] == 0x01U; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return false; });

    std::vector<uint8_t> frame = create_tb_request(422U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    // No frame yet and no response reported while waiting for the Sensing Measurement Query.
    EXPECT_EQ(stub->tb_request_count, 0U);
    EXPECT_TRUE(sensing.has_pending_tb_query(422U));
    ASSERT_EQ(sent.size(), 1U);

    em_sensing_measurement_event_t query;
    query.event_type = em_sensing_measurement_event_t::measurement_query;
    query.exchange_id = 422U;
    stub->callback(query);

    EXPECT_EQ(stub->tb_request_count, 1U);
    EXPECT_EQ(stub->last_tb_params.comeback_info, 0U);
    EXPECT_FALSE(sensing.has_pending_tb_query(422U));
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_created);
}

TEST(em_sensing_exchange, reports_timeout_when_tb_query_does_not_arrive)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t mac) { return mac[5] == 0x01U; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return false; });

    std::vector<uint8_t> frame = create_tb_request(423U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    ASSERT_EQ(sent.size(), 1U);

    sensing.process_tb_timeouts_at(std::chrono::steady_clock::now() + std::chrono::seconds(9));
    EXPECT_EQ(sent.size(), 1U);

    sensing.process_tb_timeouts_at(std::chrono::steady_clock::now() + std::chrono::seconds(11));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_timeout);
    EXPECT_EQ(stub->tb_request_count, 0U);
    EXPECT_FALSE(sensing.has_exchange(423U));
    EXPECT_FALSE(sensing.has_pending_tb_query(423U));
}

TEST(em_sensing_exchange, reports_declined_when_tb_response_is_not_success)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    stub->tb_status_code = 37U;
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t mac) { return mac[5] == 0x01U; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_tb_request(424U);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_request_declined);
    EXPECT_FALSE(sensing.has_exchange(424U));
}

TEST(em_sensing_exchange, explicitly_terminates_tb_exchange)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    lower_layer->emit_tb_response = false;
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t mac) { return mac[5] == 0x01U; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> create_frame = create_tb_request(425U);
    sensing.process_msg(create_frame.data(), static_cast<unsigned int>(create_frame.size()));
    ASSERT_TRUE(sensing.has_exchange(425U));

    em_sensing_exchange_req_t terminate{};
    terminate.exchange_id = 425U;
    terminate.exchange_type = em_sensing_exchange_tb;
    std::vector<uint8_t> terminate_frame = make_exchange_request(terminate);
    sensing.process_msg(terminate_frame.data(), static_cast<unsigned int>(terminate_frame.size()));

    EXPECT_FALSE(sensing.has_exchange(425U));
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_terminated);
}

TEST(em_sensing_exchange, sensing_mq_rejects_unknown_agent_sta)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 1U};
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 2U};
    std::vector<uint8_t> frame = create_mq_request(agent_sta, bssid);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_GT(sent.back().size(), sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
}

TEST(em_sensing_exchange, sensing_mq_completes_on_measurement_request)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 1U};
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 2U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 1U});
    sensing.set_agent_sta_interfaces({radio});
    std::vector<uint8_t> frame = create_mq_request(agent_sta, bssid);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    ASSERT_EQ(stub->mq_query_count, 1U);
    em_sensing_measurement_event_t event;
    event.event_type = em_sensing_measurement_event_t::measurement_request;
    std::memcpy(event.transmitter, agent_sta, sizeof(mac_address_t));
    std::memcpy(event.receiver, bssid, sizeof(mac_address_t));
    event.has_measurement_parameters = true;
    stub->callback(event);
    EXPECT_EQ(sensing.sensing_mq_result(agent_sta, bssid), em_sensing_mq_success);
    EXPECT_EQ(sent.size(), 2U);
}

TEST(em_sensing_exchange, sensing_mq_retries_comeback_using_exponent)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 1U};
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 2U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 1U});
    sensing.set_agent_sta_interfaces({radio});
    std::vector<uint8_t> frame = create_mq_request(agent_sta, bssid);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    em_sensing_measurement_event_t event;
    event.event_type = em_sensing_measurement_event_t::measurement_request;
    std::memcpy(event.transmitter, agent_sta, sizeof(mac_address_t));
    std::memcpy(event.receiver, bssid, sizeof(mac_address_t));
    event.comeback_info = 1U;
    event.comeback_after_exponent = 2U;
    stub->callback(event);
    EXPECT_EQ(stub->mq_query_count, 2U);
}

TEST(em_sensing_exchange, sensing_mq_reports_no_response_and_timeout)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 1U};
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 2U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 1U});
    sensing.set_agent_sta_interfaces({radio});
    std::vector<uint8_t> frame = create_mq_request(agent_sta, bssid);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    sensing.process_sensing_mq_timers_at(std::chrono::steady_clock::now() + std::chrono::seconds(2));
    EXPECT_EQ(sensing.sensing_mq_result(agent_sta, bssid), em_sensing_mq_no_response);
}

TEST(em_sensing_exchange, non_tb_transmits_when_transmitter_is_local_agent_sta)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    const mac_address_t local = {2U, 0U, 0U, 0U, 0U, 11U};
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 12U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 11U});
    sensing.set_agent_sta_interfaces({radio});
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_non_tb_request(430U, local, bssid);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    EXPECT_EQ(stub->tb_request_count, 1U);
    EXPECT_FALSE(stub->last_tb_params.sensing_transmitter);
    EXPECT_TRUE(stub->last_tb_params.sensing_receiver);
}

TEST(em_sensing_exchange, non_tb_transmits_when_receiver_is_local_backhaul)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 13U};
    const mac_address_t local = {2U, 0U, 0U, 0U, 0U, 14U};
    sensing.set_local_bss_callback([](const mac_address_t mac) { return mac[5] == 14U; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_non_tb_request(431U, bssid, local);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    EXPECT_EQ(stub->tb_request_count, 1U);
    EXPECT_TRUE(stub->last_tb_params.sensing_transmitter);
    EXPECT_FALSE(stub->last_tb_params.sensing_receiver);
}

TEST(em_sensing_exchange, rejects_non_tb_without_local_agent_or_backhaul_endpoint)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t transmitter = {2U, 0U, 0U, 0U, 0U, 15U};
    const mac_address_t receiver = {2U, 0U, 0U, 0U, 0U, 16U};
    std::vector<uint8_t> frame = create_non_tb_request(432U, transmitter, receiver);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_device_unavailable);
    EXPECT_FALSE(sensing.has_exchange(432U));
}

TEST(em_sensing_exchange, non_tb_decline_and_explicit_termination_are_reported)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    stub->emit_tb_response = false;
    stub->tb_status_code = 37U;
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t local = {2U, 0U, 0U, 0U, 0U, 17U};
    const mac_address_t peer = {2U, 0U, 0U, 0U, 0U, 18U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 17U});
    sensing.set_agent_sta_interfaces({radio});
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> frame = create_non_tb_request(433U, local, peer);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    ASSERT_TRUE(sensing.has_exchange(433U));
    em_sensing_measurement_event_t response;
    response.event_type = em_sensing_measurement_event_t::measurement_response;
    response.exchange_id = 433U;
    response.status_code = 37U;
    stub->callback(response);
    ASSERT_EQ(sent.size(), 2U);
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_request_declined);

    frame = create_non_tb_request(434U, local, peer);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));
    em_sensing_exchange_req_t terminate{};
    terminate.exchange_id = 434U;
    terminate.exchange_type = em_sensing_exchange_non_tb;
    std::vector<uint8_t> terminate_frame = make_exchange_request(terminate);
    sensing.process_msg(terminate_frame.data(), static_cast<unsigned int>(terminate_frame.size()));
    EXPECT_FALSE(sensing.has_exchange(434U));
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_terminated);
}

TEST(em_sensing_exchange, trigger_probe_rejects_unknown_agent_sta_with_error_ack)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t unknown = {2U, 0U, 0U, 0U, 0U, 21U};
    std::vector<uint8_t> frame = create_trigger_probe_request(unknown, {});
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(sent.size(), 1U);
    const auto *tlv = reinterpret_cast<const em_tlv_t *>(sent.back().data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    EXPECT_EQ(tlv->type, em_tlv_type_error_code);
    ASSERT_EQ(ntohs(tlv->len), sizeof(em_error_code_t));
    const auto *error = reinterpret_cast<const em_error_code_t *>(tlv->value);
    EXPECT_EQ(error->reason_code, 0x07U);
    EXPECT_EQ(stub->probe_request_count, 0U);
}

TEST(em_sensing_exchange, trigger_probe_sends_one_broadcast_probe)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 22U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 22U});
    sensing.set_agent_sta_interfaces({radio});
    std::vector<uint8_t> frame = create_trigger_probe_request(agent_sta, {});
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    EXPECT_EQ(stub->probe_request_count, 1U);
    EXPECT_TRUE(stub->last_probe_bssids.empty());
}

TEST(em_sensing_exchange, trigger_probe_sends_targeted_bssid_list)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    sensing.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 23U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 23U});
    sensing.set_agent_sta_interfaces({radio});
    const std::vector<std::array<uint8_t, 6>> bssids = {
        {2U, 0U, 0U, 0U, 0U, 24U}, {2U, 0U, 0U, 0U, 0U, 25U}};
    std::vector<uint8_t> frame = create_trigger_probe_request(agent_sta, bssids);
    sensing.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    EXPECT_EQ(stub->probe_request_count, 1U);
    EXPECT_EQ(stub->last_probe_bssids, bssids);
}

TEST(em_sensing_exchange, trigger_probe_response_is_tunneled_with_source_info)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    em_sensing_measurement_event_t event;
    event.event_type = em_sensing_measurement_event_t::probe_response;
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 26U};
    std::memcpy(event.transmitter, agent_sta, sizeof(mac_address_t));
    event.data = {0x50U, 0x00U, 0x01U, 0x02U};
    stub->callback(event);

    ASSERT_EQ(sent.size(), 1U);
    const auto *cmdu = reinterpret_cast<const em_cmdu_t *>(sent.back().data() + sizeof(em_raw_hdr_t));
    EXPECT_EQ(ntohs(cmdu->type), em_msg_type_tunneled);
    const auto *source_tlv = reinterpret_cast<const em_tlv_t *>(sent.back().data() +
        sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    EXPECT_EQ(source_tlv->type, em_tlv_type_src_info);
    const auto *type_tlv = reinterpret_cast<const em_tlv_t *>(source_tlv->value + sizeof(em_source_info_t));
    EXPECT_EQ(type_tlv->type, em_tlv_type_tunneled_msg_type);
    EXPECT_EQ(type_tlv->value[0], EM_TUNNELED_PROTOCOL_SENSING_PROBE_RESPONSE);
}

TEST(em_sensing_exchange, trigger_probe_failure_reports_status_and_failed_bssids)
{
    em_sensing_t agent;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    lower_layer->probe_request_accept = false;
    agent.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> agent_sent;
    agent.set_send_callback([&agent_sent](unsigned char *data, unsigned int length) {
        agent_sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 27U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 27U});
    agent.set_agent_sta_interfaces({radio});
    const std::vector<std::array<uint8_t, 6>> bssids = {{2U, 0U, 0U, 0U, 0U, 28U}};
    std::vector<uint8_t> frame = create_trigger_probe_request(agent_sta, bssids);
    agent.process_msg(frame.data(), static_cast<unsigned int>(frame.size()));

    ASSERT_EQ(agent_sent.size(), 2U);
    em_sensing_t controller;
    controller.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    controller.process_msg(agent_sent.back().data(), static_cast<unsigned int>(agent_sent.back().size()));
    EXPECT_EQ(controller.last_trigger_probe_status(), 1U);
    EXPECT_EQ(controller.last_trigger_probe_failures(), bssids);
}

TEST(em_sensing_exchange, controller_records_tunneled_probe_response_body)
{
    em_sensing_t agent;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    agent.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> tunneled;
    agent.set_send_callback([&tunneled](unsigned char *data, unsigned int length) {
        tunneled.emplace_back(data, data + length);
        return 0;
    });
    em_sensing_measurement_event_t event;
    event.event_type = em_sensing_measurement_event_t::probe_response;
    event.transmitter[5] = 29U;
    event.data = {0x50U, 0x00U, 0xAAU, 0xBBU};
    stub->callback(event);
    ASSERT_EQ(tunneled.size(), 1U);

    em_sensing_t controller;
    controller.set_send_callback([](unsigned char *, unsigned int) { return 0; });
    controller.process_msg(tunneled.front().data(), static_cast<unsigned int>(tunneled.front().size()));
    EXPECT_EQ(controller.last_tunneled_probe_response(), event.data);
}

TEST(em_sensing_integration, basic_qos_null_exchange_lifecycle)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    sensing.set_local_bss_callback([](const mac_address_t) { return true; });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });

    std::vector<uint8_t> create = create_valid_qos_null_request(500U, 0U);
    sensing.process_msg(create.data(), static_cast<unsigned int>(create.size()));
    ASSERT_TRUE(sensing.has_exchange(500U));
    sensing.process_qos_null_timers();
    ASSERT_FALSE(sensing.has_exchange(500U));
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(response_result(sent.back()), em_sensing_exchange_terminated);
}

TEST(em_sensing_integration, basic_capability_path_agent_sta_and_qos_workflow)
{
    em_sensing_t controller;
    em_sensing_t agent;
    auto controller_ll = std::make_unique<qos_null_test_ll_t>();
    auto agent_ll = std::make_unique<qos_null_test_ll_t>();
    agent.set_lower_layer(std::move(agent_ll));
    controller.set_lower_layer(std::move(controller_ll));

    std::vector<std::vector<uint8_t>> controller_received;
    std::vector<std::vector<uint8_t>> agent_received;
    controller.set_send_callback([&controller_received](unsigned char *data, unsigned int length) {
        controller_received.emplace_back(data, data + length);
        return 0;
    });
    agent.set_send_callback([&agent_received](unsigned char *data, unsigned int length) {
        agent_received.emplace_back(data, data + length);
        return 0;
    });

    // Capability report: encode the agent capability TLV and parse it on the controller.
    unsigned char capability_value[256] = {0};
    const mac_address_t ruid = {0U, 0U, 0U, 0U, 0U, 0U};
    const unsigned short capability_length = agent.create_sensing_capability_tlv(capability_value, ruid);
    ASSERT_GT(capability_length, 0U);
    std::vector<uint8_t> capability_tlv = {
        static_cast<uint8_t>(em_tlv_type_sensing_cap),
        static_cast<uint8_t>(capability_length >> 8U),
        static_cast<uint8_t>(capability_length & 0xffU)};
    capability_tlv.insert(capability_tlv.end(), capability_value, capability_value + capability_length);
    EXPECT_EQ(controller.handle_sensing_capability_tlv(em_tlv_type_sensing_cap,
        capability_tlv.data(), capability_tlv.size()), 0);

    em_raw_hdr_t route{};
    const mac_address_t controller_mac = {2U, 0U, 0U, 0U, 0U, 61U};
    const mac_address_t agent_mac = {2U, 0U, 0U, 0U, 0U, 62U};
    std::memcpy(route.src, controller_mac, sizeof(mac_address_t));
    std::memcpy(route.dst, agent_mac, sizeof(mac_address_t));

    // Layer3 Path setup: the agent receives the request and emits Ack + Response.
    dm_layer3_path_info_t path;
    path.service_name = em_layer3_service_sensing;
    path.transport_protocol = em_layer3_transport_udp_ipv4;
    path.destination_address[12] = 192U;
    path.destination_address[13] = 0U;
    path.destination_address[14] = 2U;
    path.destination_address[15] = 60U;
    path.destination_port = 5060U;
    ASSERT_TRUE(controller.send_layer3_path_setup(route, path, true));
    ASSERT_FALSE(controller_received.empty());
    agent.process_msg(controller_received.back().data(),
        static_cast<unsigned int>(controller_received.back().size()));
    ASSERT_GE(agent_received.size(), 2U);
    controller.process_msg(agent_received.back().data(),
        static_cast<unsigned int>(agent_received.back().size()));

    // Agent STA interface configuration: the agent handles the request and reports back.
    controller_received.clear();
    agent_received.clear();
    ASSERT_TRUE(controller.send_agent_sta_iface_config(route, ruid, 1U));
    agent.process_msg(controller_received.back().data(),
        static_cast<unsigned int>(controller_received.back().size()));
    ASSERT_GE(agent_received.size(), 2U);

    // QoS Null exchange: create, deliver a phase, and explicitly terminate.
    agent.set_local_bss_callback([](const mac_address_t) { return true; });
    agent.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return true; });
    std::vector<uint8_t> exchange = create_valid_qos_null_request(502U, 0U);
    agent.process_msg(exchange.data(), static_cast<unsigned int>(exchange.size()));
    ASSERT_TRUE(agent.has_exchange(502U));
    agent.process_qos_null_timers();
    ASSERT_FALSE(agent.has_exchange(502U));
}

TEST(em_sensing_integration, advanced_probe_mq_and_unassociated_tb_lifecycle)
{
    em_sensing_t sensing;
    auto lower_layer = std::make_unique<qos_null_test_ll_t>();
    qos_null_test_ll_t *stub = lower_layer.get();
    sensing.set_lower_layer(std::move(lower_layer));
    std::vector<std::vector<uint8_t>> sent;
    sensing.set_send_callback([&sent](unsigned char *data, unsigned int length) {
        sent.emplace_back(data, data + length);
        return 0;
    });
    const mac_address_t agent_sta = {2U, 0U, 0U, 0U, 0U, 50U};
    const mac_address_t bssid = {2U, 0U, 0U, 0U, 0U, 51U};
    em_agent_sta_iface_radio_view_t radio;
    radio.agent_sta_mac_addresses.push_back({2U, 0U, 0U, 0U, 0U, 50U});
    sensing.set_agent_sta_interfaces({radio});
    sensing.set_local_bss_callback([bssid](const mac_address_t address) {
        return std::memcmp(address, bssid, sizeof(mac_address_t)) == 0;
    });
    sensing.set_associated_sta_callback([](const mac_address_t, const mac_address_t) { return false; });

    std::vector<uint8_t> probe = create_trigger_probe_request(agent_sta, {});
    sensing.process_msg(probe.data(), static_cast<unsigned int>(probe.size()));
    ASSERT_EQ(stub->probe_request_count, 1U);

    std::vector<uint8_t> mq = create_mq_request(agent_sta, bssid);
    sensing.process_msg(mq.data(), static_cast<unsigned int>(mq.size()));
    ASSERT_EQ(stub->mq_query_count, 1U);
    em_sensing_measurement_event_t mq_request;
    mq_request.event_type = em_sensing_measurement_event_t::measurement_request;
    std::memcpy(mq_request.transmitter, agent_sta, sizeof(mac_address_t));
    std::memcpy(mq_request.receiver, bssid, sizeof(mac_address_t));
    mq_request.has_measurement_parameters = true;
    stub->callback(mq_request);
    EXPECT_EQ(sensing.sensing_mq_result(agent_sta, bssid), em_sensing_mq_success);

    std::vector<uint8_t> tb = create_tb_request(501U);
    auto *tb_tlv = reinterpret_cast<em_tlv_t *>(tb.data() + sizeof(em_raw_hdr_t) + sizeof(em_cmdu_t));
    auto *tb_request = reinterpret_cast<em_sensing_exchange_req_t *>(tb_tlv->value);
    std::memcpy(tb_request->transmitter, bssid, sizeof(mac_address_t));
    std::memcpy(tb_request->receiver, agent_sta, sizeof(mac_address_t));
    sensing.process_msg(tb.data(), static_cast<unsigned int>(tb.size()));
    ASSERT_TRUE(sensing.has_exchange(501U));
    EXPECT_TRUE(sensing.has_pending_tb_query(501U));
    em_sensing_measurement_event_t query;
    query.event_type = em_sensing_measurement_event_t::measurement_query;
    query.exchange_id = 501U;
    stub->callback(query);
    EXPECT_EQ(stub->tb_request_count, 1U);

    em_sensing_measurement_event_t measurement_response;
    measurement_response.event_type = em_sensing_measurement_event_t::measurement_response;
    measurement_response.exchange_id = 501U;
    measurement_response.status_code = EM_SENSING_STATUS_SUCCESS;
    stub->callback(measurement_response);
    ASSERT_TRUE(sensing.has_exchange(501U));
    em_sensing_exchange_req_t terminate{};
    terminate.exchange_id = 501U;
    terminate.exchange_type = em_sensing_exchange_tb;
    std::vector<uint8_t> terminate_frame = make_exchange_request(terminate);
    sensing.process_msg(terminate_frame.data(), static_cast<unsigned int>(terminate_frame.size()));
    EXPECT_FALSE(sensing.has_exchange(501U));
}