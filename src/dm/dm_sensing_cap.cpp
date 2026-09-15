#include "dm_sensing_cap.h"

#include <algorithm>
#include <cstring>

int dm_sensing_cap_t::init() { m_info = {}; return 0; }
int dm_sensing_cap_t::decode(const cJSON *obj, void *)
{
    if (obj == nullptr) { return -1; }
    init();
    const cJSON *item = nullptr;
    if ((item = cJSON_GetObjectItem(obj, "Layer3TransportFlags")) != nullptr) { m_info.layer3_transport_flags = static_cast<uint8_t>(item->valueint); }
    if ((item = cJSON_GetObjectItem(obj, "BSSFlags")) != nullptr) { m_info.bss_flags = static_cast<uint8_t>(item->valueint); }
    if ((item = cJSON_GetObjectItem(obj, "STAFlags")) != nullptr) { m_info.sta_flags = static_cast<uint8_t>(item->valueint); }
    if ((item = cJSON_GetObjectItem(obj, "NumDataTypes")) != nullptr) { m_info.num_data_types = static_cast<uint8_t>(item->valueint); }
    const cJSON *types = cJSON_GetObjectItem(obj, "DataTypes");
    if (cJSON_IsArray(types)) {
        m_info.num_data_types = static_cast<uint8_t>(std::min(cJSON_GetArraySize(types), 8));
        for (uint8_t index = 0U; index < m_info.num_data_types; ++index) { m_info.data_types[index] = static_cast<uint32_t>(cJSON_GetArrayItem(types, index)->valuedouble); }
    }
    return 0;
}
void dm_sensing_cap_t::encode(cJSON *obj) const
{
    if (obj == nullptr) { return; }
    cJSON_AddNumberToObject(obj, "Layer3TransportFlags", m_info.layer3_transport_flags);
    cJSON_AddNumberToObject(obj, "BSSFlags", m_info.bss_flags);
    cJSON_AddNumberToObject(obj, "STAFlags", m_info.sta_flags);
    cJSON *types = cJSON_AddArrayToObject(obj, "DataTypes");
    for (uint8_t index = 0U; index < m_info.num_data_types && index < 8U; ++index) { cJSON_AddItemToArray(types, cJSON_CreateNumber(m_info.data_types[index])); }
}
bool dm_sensing_cap_t::operator==(const dm_sensing_cap_t &other) const
{
    return std::memcmp(&m_info, &other.m_info, sizeof(m_info)) == 0;
}
dm_sensing_cap_t &dm_sensing_cap_t::operator=(const dm_sensing_cap_t &other)
{
    if (this != &other) { std::memcpy(&m_info, &other.m_info, sizeof(m_info)); }
    return *this;
}
