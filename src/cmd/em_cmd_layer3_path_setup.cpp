#include "em_cmd_layer3_path_setup.h"

#include <cstdio>
#include <cstring>

em_cmd_layer3_path_setup_t::em_cmd_layer3_path_setup_t(em_cmd_params_t param, dm_easy_mesh_t &dm)
{
    m_type = em_cmd_type_layer3_path_setup;
    std::memcpy(&m_param, &param, sizeof(m_param));
    std::memset(m_orch_desc, 0, sizeof(m_orch_desc));
    m_orch_op_idx = 0U;
    m_num_orch_desc = 1U;
    m_orch_desc[0].op = dm_orch_type_none;
    m_orch_desc[0].submit = true;
    std::snprintf(m_name, sizeof(m_name), "%s", "sensing_layer3_path");
    m_svc = em_service_type_ctrl;
    init(dm);
}
