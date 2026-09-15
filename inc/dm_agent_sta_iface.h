#ifndef DM_AGENT_STA_IFACE_H
#define DM_AGENT_STA_IFACE_H

#include "em_base.h"

struct dm_agent_sta_iface_info_t {
    mac_address_t ruid{};
    uint8_t num_sta = 0U;
    mac_address_t agent_sta_mac[EM_MAX_RADIO_PER_AGENT]{};
};

class dm_agent_sta_iface_t {
public:
    dm_agent_sta_iface_info_t m_info;

    int init();
    int decode(const cJSON *obj, void *parent_id);
    void encode(cJSON *obj) const;
    bool operator==(const dm_agent_sta_iface_t &other) const;
    dm_agent_sta_iface_t &operator=(const dm_agent_sta_iface_t &other);
};

#endif
