#ifndef DM_SENSING_CAP_H
#define DM_SENSING_CAP_H

#include "em_sensing_ll.h"

struct dm_sensing_cap_info_t {
    mac_address_t ruid{};
    uint8_t layer3_transport_flags = 0U;
    uint8_t bss_flags = 0U;
    uint8_t sta_flags = 0U;
    uint8_t bss_capabilities[9]{};
    uint8_t sta_capabilities[9]{};
    uint8_t num_data_types = 0U;
    uint32_t data_types[8]{};
};

class dm_sensing_cap_t {
public:
    dm_sensing_cap_info_t m_info;

    int init();
    int decode(const cJSON *obj, void *parent_id);
    void encode(cJSON *obj) const;
    bool operator==(const dm_sensing_cap_t &other) const;
    dm_sensing_cap_t &operator=(const dm_sensing_cap_t &other);
};

#endif
