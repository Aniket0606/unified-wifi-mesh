#ifndef EM_SENSING_PATH_H
#define EM_SENSING_PATH_H

#include "dm_layer3_path.h"
#include "em_sensing_l3.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

class em_sensing_path_manager_t {
public:
    static constexpr size_t max_paths = 8U;

    em_sensing_path_manager_t();
    ~em_sensing_path_manager_t();

    bool add_path(const em_layer3_path_setup_req_t &request, dm_layer3_path_info_t &result);
    bool remove_path(const em_layer3_path_setup_req_t &request, dm_layer3_path_info_t &result);
    bool send_measurement(const dm_layer3_path_info_t &path,
        const em_sensing_measurement_input_t &measurement);
    void close_all();
    size_t size() const { return m_paths.size(); }

private:
    struct path_entry_t {
        dm_layer3_path_info_t info;
        int socket_fd = -1;
    };

    std::vector<path_entry_t> m_paths;
};

#endif
