#include "dm_layer3_path.h"

#include <cstring>

int dm_layer3_path_t::init() { m_info = {}; return 0; }
int dm_layer3_path_t::decode(const cJSON *obj, void *)
{
    if (obj == nullptr) { return -1; }
    init();
    const cJSON *item = cJSON_GetObjectItem(obj, "ServiceName"); if (item) { m_info.service_name = static_cast<uint16_t>(item->valueint); }
    item = cJSON_GetObjectItem(obj, "TransportProtocol"); if (item) { m_info.transport_protocol = static_cast<uint8_t>(item->valueint); }
    item = cJSON_GetObjectItem(obj, "DestinationPort"); if (item) { m_info.destination_port = static_cast<uint16_t>(item->valueint); }
    item = cJSON_GetObjectItem(obj, "SourcePort"); if (item) { m_info.source_port = static_cast<uint16_t>(item->valueint); }
    item = cJSON_GetObjectItem(obj, "Active"); if (item) { m_info.active = cJSON_IsTrue(item); }
    return 0;
}
void dm_layer3_path_t::encode(cJSON *obj) const { if (obj) { cJSON_AddNumberToObject(obj, "ServiceName", m_info.service_name); cJSON_AddNumberToObject(obj, "TransportProtocol", m_info.transport_protocol); cJSON_AddNumberToObject(obj, "DestinationPort", m_info.destination_port); cJSON_AddNumberToObject(obj, "SourcePort", m_info.source_port); cJSON_AddBoolToObject(obj, "Active", m_info.active); } }
bool dm_layer3_path_t::operator==(const dm_layer3_path_t &other) const
{
    return std::memcmp(&m_info, &other.m_info, sizeof(m_info)) == 0;
}
dm_layer3_path_t &dm_layer3_path_t::operator=(const dm_layer3_path_t &other)
{
    if (this != &other) { std::memcpy(&m_info, &other.m_info, sizeof(m_info)); }
    return *this;
}
