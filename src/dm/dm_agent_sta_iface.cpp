#include "dm_agent_sta_iface.h"
#include "dm_easy_mesh.h"

#include <algorithm>
#include <cstring>

int dm_agent_sta_iface_t::init() { m_info = {}; return 0; }
int dm_agent_sta_iface_t::decode(const cJSON *obj, void *)
{
    if (obj == nullptr) { return -1; }
    init();
    const cJSON *item = cJSON_GetObjectItem(obj, "NumSTA");
    if (item != nullptr) { m_info.num_sta = static_cast<uint8_t>(std::min(item->valueint, EM_MAX_RADIO_PER_AGENT)); }
    const cJSON *macs = cJSON_GetObjectItem(obj, "AgentSTAMAC");
    if (cJSON_IsArray(macs)) {
        m_info.num_sta = static_cast<uint8_t>(std::min(cJSON_GetArraySize(macs), EM_MAX_RADIO_PER_AGENT));
        for (uint8_t index = 0U; index < m_info.num_sta; ++index) { if (cJSON_IsString(cJSON_GetArrayItem(macs, index))) { dm_easy_mesh_t::string_to_macbytes(cJSON_GetArrayItem(macs, index)->valuestring, m_info.agent_sta_mac[index]); } }
    }
    return 0;
}
void dm_agent_sta_iface_t::encode(cJSON *obj) const
{
    if (obj == nullptr) { return; }
    cJSON_AddNumberToObject(obj, "NumSTA", m_info.num_sta);
}
bool dm_agent_sta_iface_t::operator==(const dm_agent_sta_iface_t &other) const
{
    return std::memcmp(&m_info, &other.m_info, sizeof(m_info)) == 0;
}
dm_agent_sta_iface_t &dm_agent_sta_iface_t::operator=(const dm_agent_sta_iface_t &other)
{
    if (this != &other) { std::memcpy(&m_info, &other.m_info, sizeof(m_info)); }
    return *this;
}
