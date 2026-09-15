#ifndef EM_CMD_TRIGGER_PROBE_H
#define EM_CMD_TRIGGER_PROBE_H

#include "em_cmd.h"

class em_cmd_trigger_probe_t : public em_cmd_t {
public:
    em_cmd_trigger_probe_t(em_cmd_params_t param, dm_easy_mesh_t &dm);
};

#endif
