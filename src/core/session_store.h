#pragma once

#include "core/session.h"

#include <filesystem>
#include <optional>
#include <vector>

class SessionStore {
public:
    SessionStore();

    std::vector<SessionData> load_all() const;
    std::optional<SessionData> load(const std::string& id) const;
    bool save(const SessionData& data) const;
    bool remove(const std::string& id) const;

    std::string active_id() const;
    void set_active_id(const std::string& id) const;

    static std::string new_id();

private:
    std::filesystem::path directory_;
    std::filesystem::path sessions_directory_;
    std::filesystem::path active_path_;

    static std::optional<SessionData> from_json(const nlohmann::json& value);
    static nlohmann::json to_json(const SessionData& data);
    std::filesystem::path session_path(const std::string& id) const;
};
