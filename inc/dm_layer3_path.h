#ifndef DM_LAYER3_PATH_H
#define DM_LAYER3_PATH_H

#include "em_base.h"

struct dm_layer3_path_info_t {
    uint16_t service_name = em_layer3_service_sensing;
    uint8_t transport_protocol = em_layer3_transport_udp_ipv6;
    uint8_t destination_address[16]{};
    uint16_t destination_port = 0U;
    uint8_t source_address[16]{};
    uint16_t source_port = 0U;
    bool active = false;
};

class dm_layer3_path_t {
public:
    dm_layer3_path_info_t m_info;

    int init();
    int decode(const cJSON *obj, void *parent_id);
    void encode(cJSON *obj) const;
    bool operator==(const dm_layer3_path_t &other) const;
    dm_layer3_path_t &operator=(const dm_layer3_path_t &other);
};

#endif
