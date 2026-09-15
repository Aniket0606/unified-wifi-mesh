#include "dm_sensing_exchange.h"

#include <cstring>

int dm_sensing_exchange_t::init() { m_info = {}; return 0; }
int dm_sensing_exchange_t::decode(const cJSON *obj, void *)
{
    if (obj == nullptr) { return -1; }
    init();
    const cJSON *item = nullptr;
    if ((item = cJSON_GetObjectItem(obj, "ExchangeID")) != nullptr) { m_info.exchange_id = static_cast<uint32_t>(item->valuedouble); }
    if ((item = cJSON_GetObjectItem(obj, "ExchangeType")) != nullptr) { m_info.exchange_type = static_cast<uint8_t>(item->valueint); }
    if ((item = cJSON_GetObjectItem(obj, "MeasurementsRequested")) != nullptr) { m_info.measurements_requested = cJSON_IsTrue(item); }
    if ((item = cJSON_GetObjectItem(obj, "Period")) != nullptr) { m_info.period = static_cast<uint16_t>(item->valueint); }
    if ((item = cJSON_GetObjectItem(obj, "Bandwidth")) != nullptr) { m_info.bandwidth = static_cast<uint16_t>(item->valueint); }
    if ((item = cJSON_GetObjectItem(obj, "DataType")) != nullptr) { m_info.data_type = static_cast<uint32_t>(item->valuedouble); }
    if ((item = cJSON_GetObjectItem(obj, "ResultCode")) != nullptr) { m_info.result_code = static_cast<uint8_t>(item->valueint); }
    return 0;
}
void dm_sensing_exchange_t::encode(cJSON *obj) const { if (obj) { cJSON_AddNumberToObject(obj, "ExchangeID", m_info.exchange_id); cJSON_AddNumberToObject(obj, "ExchangeType", m_info.exchange_type); cJSON_AddBoolToObject(obj, "MeasurementsRequested", m_info.measurements_requested); cJSON_AddNumberToObject(obj, "Period", m_info.period); cJSON_AddNumberToObject(obj, "Bandwidth", m_info.bandwidth); cJSON_AddNumberToObject(obj, "DataType", m_info.data_type); cJSON_AddNumberToObject(obj, "ResultCode", m_info.result_code); } }
bool dm_sensing_exchange_t::operator==(const dm_sensing_exchange_t &other) const
{
    return std::memcmp(&m_info, &other.m_info, sizeof(m_info)) == 0;
}
dm_sensing_exchange_t &dm_sensing_exchange_t::operator=(const dm_sensing_exchange_t &other)
{
    if (this != &other) { std::memcpy(&m_info, &other.m_info, sizeof(m_info)); }
    return *this;
}
