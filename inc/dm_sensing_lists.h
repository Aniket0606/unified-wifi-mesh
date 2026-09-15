#ifndef DM_SENSING_LISTS_H
#define DM_SENSING_LISTS_H

#include "dm_sensing_cap.h"
#include "dm_agent_sta_iface.h"
#include "dm_layer3_path.h"
#include "dm_sensing_exchange.h"

#include <vector>

class dm_sensing_cap_list_t {
public:
    std::vector<dm_sensing_cap_t> entries;
    void clear() { entries.clear(); }
};

class dm_agent_sta_iface_list_t {
public:
    std::vector<dm_agent_sta_iface_t> entries;
    void clear() { entries.clear(); }
};

class dm_layer3_path_list_t {
public:
    std::vector<dm_layer3_path_t> entries;
    void clear() { entries.clear(); }
};

class dm_sensing_exchange_list_t {
public:
    std::vector<dm_sensing_exchange_t> entries;
    void clear() { entries.clear(); }
};

#endif
