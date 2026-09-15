#ifndef DM_SENSING_EXCHANGE_H
#define DM_SENSING_EXCHANGE_H

#include "em_base.h"

struct dm_sensing_exchange_info_t {
    uint32_t exchange_id = 0U;
    uint8_t exchange_type = em_sensing_exchange_qos_null;
    bool add_exchange = false;
    bool measurements_requested = false;
    uint16_t period = 0U;
    uint16_t bandwidth = 0U;
    uint8_t n_tx = 0U;
    uint8_t n_rx = 0U;
    uint32_t data_type = 0U;
    mac_address_t transmitter{};
    mac_address_t receiver{};
    uint8_t result_code = em_sensing_exchange_terminated;
};

class dm_sensing_exchange_t {
public:
    dm_sensing_exchange_info_t m_info;

    int init();
    int decode(const cJSON *obj, void *parent_id);
    void encode(cJSON *obj) const;
    bool operator==(const dm_sensing_exchange_t &other) const;
    dm_sensing_exchange_t &operator=(const dm_sensing_exchange_t &other);
};

#endif
