#ifndef EM_CMD_AGENT_STA_IFACE_CONFIG_H
#define EM_CMD_AGENT_STA_IFACE_CONFIG_H

#include "em_cmd.h"

class em_cmd_agent_sta_iface_config_t : public em_cmd_t {
public:
    em_cmd_agent_sta_iface_config_t(em_cmd_params_t param, dm_easy_mesh_t &dm);
};

#endif
