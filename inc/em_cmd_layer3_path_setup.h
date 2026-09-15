#ifndef EM_CMD_LAYER3_PATH_SETUP_H
#define EM_CMD_LAYER3_PATH_SETUP_H

#include "em_cmd.h"

class em_cmd_layer3_path_setup_t : public em_cmd_t {
public:
    em_cmd_layer3_path_setup_t(em_cmd_params_t param, dm_easy_mesh_t &dm);
};

#endif
