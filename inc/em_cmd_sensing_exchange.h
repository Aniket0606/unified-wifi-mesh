#ifndef EM_CMD_SENSING_EXCHANGE_H
#define EM_CMD_SENSING_EXCHANGE_H

#include "em_cmd.h"

class em_cmd_sensing_exchange_t : public em_cmd_t {
public:
    em_cmd_sensing_exchange_t(em_cmd_params_t param, dm_easy_mesh_t &dm);
};

#endif
