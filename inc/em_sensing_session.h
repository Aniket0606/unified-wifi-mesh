#ifndef EM_SENSING_SESSION_H
#define EM_SENSING_SESSION_H

#include "em_sensing_l3.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

class em_sensing_session_manager_t {
public:
    em_sensing_session_manager_t();
    ~em_sensing_session_manager_t();

    bool create_session(std::string &socket_path);
    bool delete_session(const std::string &socket_path);
    bool add_exchange(const std::string &socket_path, uint32_t exchange_id);
    bool remove_exchange(const std::string &socket_path, uint32_t exchange_id);
    bool publish_measurement(const em_sensing_measurement_input_t &measurement);
    bool notify_exchange_terminated(uint32_t exchange_id, uint8_t result_code);
    size_t session_count() const { return m_sessions.size(); }

private:
    struct session_t {
        int socket_fd = -1;
        std::string socket_path;
        std::unordered_set<uint32_t> exchanges;
    };

    std::unordered_map<std::string, session_t> m_sessions;
    std::string m_directory;

    bool send_to_session(const session_t &session, const uint8_t *data, size_t length) const;
};

#endif
