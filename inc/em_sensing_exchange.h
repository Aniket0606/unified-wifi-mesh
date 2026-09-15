#ifndef EM_SENSING_EXCHANGE_H
#define EM_SENSING_EXCHANGE_H

#include "dm_sensing_exchange.h"

#include <cstddef>
#include <functional>
#include <unordered_map>

class em_sensing_exchange_manager_t {
public:
    static constexpr size_t max_exchanges = 32U;

    bool add(const dm_sensing_exchange_info_t &exchange);
    bool remove(uint32_t exchange_id, uint8_t result_code);
    bool contains(uint32_t exchange_id) const;
    bool get(uint32_t exchange_id, dm_sensing_exchange_info_t &exchange) const;
    bool update_result(uint32_t exchange_id, uint8_t result_code);
    void remove_matching(const std::function<bool(const dm_sensing_exchange_info_t &)> &predicate,
        uint8_t result_code, std::vector<dm_sensing_exchange_info_t> &removed);
    size_t size() const { return m_exchanges.size(); }

private:
    std::unordered_map<uint32_t, dm_sensing_exchange_info_t> m_exchanges;
};

#endif
