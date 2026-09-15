#include "em_sensing_session.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
}

em_sensing_session_manager_t::em_sensing_session_manager_t()
    : m_sessions(), m_directory("/tmp/onewifi_sensing")
{
    (void)mkdir(m_directory.c_str(), 0700);
}

em_sensing_session_manager_t::~em_sensing_session_manager_t()
{
    for (const auto &entry : m_sessions) {
        close(entry.second.socket_fd);
        unlink(entry.second.socket_path.c_str());
    }
    rmdir(m_directory.c_str());
}

bool em_sensing_session_manager_t::create_session(std::string &socket_path)
{
    for (uint32_t attempt = 0U; attempt < 100U; ++attempt) {
        char path[108] = {0};
        std::snprintf(path, sizeof(path), "%s/session-%u.sock", m_directory.c_str(), attempt);
        if (m_sessions.find(path) != m_sessions.end()) {
            continue;
        }
        const int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd < 0) {
            return false;
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", path);
        unlink(address.sun_path);
        if (bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) < 0) {
            close(fd);
            continue;
        }
        chmod(address.sun_path, 0600);
        session_t session;
        session.socket_fd = fd;
        session.socket_path = path;
        m_sessions.emplace(session.socket_path, std::move(session));
        socket_path = path;
        return true;
    }
    return false;
}

bool em_sensing_session_manager_t::delete_session(const std::string &socket_path)
{
    const auto iterator = m_sessions.find(socket_path);
    if (iterator == m_sessions.end()) {
        return false;
    }
    close(iterator->second.socket_fd);
    unlink(iterator->second.socket_path.c_str());
    m_sessions.erase(iterator);
    return true;
}

bool em_sensing_session_manager_t::add_exchange(const std::string &socket_path, uint32_t exchange_id)
{
    const auto iterator = m_sessions.find(socket_path);
    return iterator != m_sessions.end() && exchange_id != 0U && iterator->second.exchanges.insert(exchange_id).second;
}

bool em_sensing_session_manager_t::remove_exchange(const std::string &socket_path, uint32_t exchange_id)
{
    const auto iterator = m_sessions.find(socket_path);
    return iterator != m_sessions.end() && iterator->second.exchanges.erase(exchange_id) != 0U;
}

bool em_sensing_session_manager_t::send_to_session(const session_t &session, const uint8_t *data, size_t length) const
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", session.socket_path.c_str());
    const ssize_t sent = sendto(session.socket_fd, data, length, 0,
        reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    return sent == static_cast<ssize_t>(length);
}

bool em_sensing_session_manager_t::publish_measurement(const em_sensing_measurement_input_t &measurement)
{
    std::vector<uint8_t> payload;
    if (!em_sensing_l3_t::encode_measurement(measurement, payload)) {
        return false;
    }
    bool delivered = false;
    for (const auto &entry : m_sessions) {
        if (entry.second.exchanges.find(measurement.exchange_id) != entry.second.exchanges.end()) {
            delivered = send_to_session(entry.second, payload.data(), payload.size()) || delivered;
        }
    }
    return delivered;
}

bool em_sensing_session_manager_t::notify_exchange_terminated(uint32_t exchange_id, uint8_t result_code)
{
    uint8_t notification[5] = {
        static_cast<uint8_t>((exchange_id >> 24U) & 0xffU),
        static_cast<uint8_t>((exchange_id >> 16U) & 0xffU),
        static_cast<uint8_t>((exchange_id >> 8U) & 0xffU),
        static_cast<uint8_t>(exchange_id & 0xffU), result_code};
    bool delivered = false;
    for (const auto &entry : m_sessions) {
        if (entry.second.exchanges.find(exchange_id) != entry.second.exchanges.end()) {
            delivered = send_to_session(entry.second, notification, sizeof(notification)) || delivered;
        }
    }
    for (auto &entry : m_sessions) {
        entry.second.exchanges.erase(exchange_id);
    }
    return delivered;
}
