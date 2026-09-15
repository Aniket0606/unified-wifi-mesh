/*
 * Copyright 2025 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#ifndef EM_SENSING_TLV_H
#define EM_SENSING_TLV_H

#include "em_base.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct em_sensing_radio_capability_view_t {
    mac_address_t ruid{};
    uint8_t bss_flags = 0;
    std::array<uint8_t, 9> bss_capabilities{};
    uint8_t sta_flags = 0;
    std::array<uint8_t, 9> sta_capabilities{};
    std::vector<uint32_t> data_types;
};

struct em_agent_sta_iface_radio_view_t {
    mac_address_t ruid{};
    std::vector<std::array<uint8_t, 6>> agent_sta_mac_addresses;
};

bool em_encode_layer3_transport_cap_tlv(uint8_t flags, std::vector<uint8_t> &tlv);
bool em_decode_layer3_transport_cap_tlv(const uint8_t *tlv, size_t tlv_len, uint8_t &flags);

bool em_encode_sensing_exchange_req_tlv(const em_sensing_exchange_req_t &value, std::vector<uint8_t> &tlv);
bool em_decode_sensing_exchange_req_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_exchange_req_t &value);

bool em_encode_sensing_exchange_rsp_tlv(const em_sensing_exchange_rsp_t &value, std::vector<uint8_t> &tlv);
bool em_decode_sensing_exchange_rsp_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_exchange_rsp_t &value);

bool em_encode_layer3_path_setup_req_tlv(const em_layer3_path_setup_req_t &value, std::vector<uint8_t> &tlv);
bool em_decode_layer3_path_setup_req_tlv(const uint8_t *tlv, size_t tlv_len, em_layer3_path_setup_req_t &value);

bool em_encode_layer3_path_setup_rsp_tlv(const em_layer3_path_setup_rsp_t &value, std::vector<uint8_t> &tlv);
bool em_decode_layer3_path_setup_rsp_tlv(const uint8_t *tlv, size_t tlv_len, em_layer3_path_setup_rsp_t &value);

bool em_encode_sensing_mq_req_tlv(const em_sensing_mq_req_t &value, std::vector<uint8_t> &tlv);
bool em_decode_sensing_mq_req_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_mq_req_t &value);

bool em_encode_sensing_mq_rsp_tlv(const em_sensing_mq_rsp_t &value, std::vector<uint8_t> &tlv);
bool em_decode_sensing_mq_rsp_tlv(const uint8_t *tlv, size_t tlv_len, em_sensing_mq_rsp_t &value);

bool em_encode_sensing_cap_tlv(const std::vector<em_sensing_radio_capability_view_t> &value, std::vector<uint8_t> &tlv);
bool em_decode_sensing_cap_tlv(const uint8_t *tlv, size_t tlv_len, std::vector<em_sensing_radio_capability_view_t> &value);

bool em_encode_agent_sta_iface_tlv(const std::vector<em_agent_sta_iface_radio_view_t> &value, std::vector<uint8_t> &tlv);
bool em_decode_agent_sta_iface_tlv(const uint8_t *tlv, size_t tlv_len, std::vector<em_agent_sta_iface_radio_view_t> &value);

bool em_encode_trigger_probe_req_tlv(const em_trigger_probe_req_t &value, const std::vector<std::array<uint8_t, 6>> &bssids, std::vector<uint8_t> &tlv);
bool em_decode_trigger_probe_req_tlv(const uint8_t *tlv, size_t tlv_len, em_trigger_probe_req_t &value, std::vector<std::array<uint8_t, 6>> &bssids);

#endif
