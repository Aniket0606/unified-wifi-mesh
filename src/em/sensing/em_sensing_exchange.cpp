#include "em_sensing_exchange.h"

bool em_sensing_exchange_manager_t::add(const dm_sensing_exchange_info_t &exchange)
{
    if (exchange.exchange_id == 0U || m_exchanges.size() >= max_exchanges ||
        m_exchanges.find(exchange.exchange_id) != m_exchanges.end()) {
        return false;
    }
    m_exchanges.emplace(exchange.exchange_id, exchange);
    return true;
}

bool em_sensing_exchange_manager_t::remove(uint32_t exchange_id, uint8_t result_code)
{
    const auto iterator = m_exchanges.find(exchange_id);
    if (iterator == m_exchanges.end()) { return false; }
    iterator->second.result_code = result_code;
    m_exchanges.erase(iterator);
    return true;
}

bool em_sensing_exchange_manager_t::contains(uint32_t exchange_id) const
{
    return m_exchanges.find(exchange_id) != m_exchanges.end();
}

bool em_sensing_exchange_manager_t::get(uint32_t exchange_id, dm_sensing_exchange_info_t &exchange) const
{
    const auto iterator = m_exchanges.find(exchange_id);
    if (iterator == m_exchanges.end()) { return false; }
    exchange = iterator->second;
    return true;
}

bool em_sensing_exchange_manager_t::update_result(uint32_t exchange_id, uint8_t result_code)
{
    const auto iterator = m_exchanges.find(exchange_id);
    if (iterator == m_exchanges.end()) { return false; }
    iterator->second.result_code = result_code;
    return true;
}

void em_sensing_exchange_manager_t::remove_matching(
    const std::function<bool(const dm_sensing_exchange_info_t &)> &predicate,
    uint8_t result_code, std::vector<dm_sensing_exchange_info_t> &removed)
{
    for (auto iterator = m_exchanges.begin(); iterator != m_exchanges.end();) {
        if (!predicate(iterator->second)) {
            ++iterator;
            continue;
        }
        iterator->second.result_code = result_code;
        removed.push_back(iterator->second);
        iterator = m_exchanges.erase(iterator);
    }
}
