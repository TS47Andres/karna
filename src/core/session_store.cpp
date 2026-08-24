#include "core/session_store.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
MessageRole role_from_string(const std::string& role)
{
    if (role == "system") return MessageRole::System;
    if (role == "assistant") return MessageRole::Assistant;
    if (role == "tool") return MessageRole::Tool;
    return MessageRole::User;
}

std::string role_to_string(MessageRole role)
{
    switch (role) {
        case MessageRole::System: return "system";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::Tool: return "tool";
        case MessageRole::User: return "user";
    }
    return "user";
}

json tool_call_to_json(const ToolCall& call)
{
    return {
        {"id", call.id},
        {"type", call.type},
        {"function_name", call.function_name},
        {"arguments", call.arguments},
        {"index", call.index}
    };
}

ToolCall tool_call_from_json(const json& value)
{
    ToolCall call;
    call.id = value.value("id", "");
    call.type = value.value("type", "");
    call.function_name = value.value("function_name", "");
    call.arguments = value.value("arguments", "");
    call.index = value.value("index", -1);
    return call;
}

json message_to_json(const Message& message)
{
    json value = {
        {"role", role_to_string(message.role)},
        {"content", message.content},
        {"tool_calls", json::array()}
    };
    if (message.name) value["name"] = *message.name;
    if (message.tool_call_id) value["tool_call_id"] = *message.tool_call_id;
    for (const auto& call : message.tool_calls) {
        value["tool_calls"].push_back(tool_call_to_json(call));
    }
    return value;
}

Message message_from_json(const json& value)
{
    Message message;
    message.role = role_from_string(value.value("role", "user"));
    message.content = value.value("content", "");
    if (value.contains("name") && value["name"].is_string()) {
        message.name = value["name"].get<std::string>();
    }
    if (value.contains("tool_call_id") && value["tool_call_id"].is_string()) {
        message.tool_call_id = value["tool_call_id"].get<std::string>();
    }
    if (value.contains("tool_calls") && value["tool_calls"].is_array()) {
        for (const auto& call : value["tool_calls"]) {
            if (call.is_object()) message.tool_calls.push_back(tool_call_from_json(call));
        }
    }
    return message;
}
}

SessionStore::SessionStore()
    : directory_(Config::storage_dir())
    , sessions_directory_(directory_ / "sessions")
    , active_path_(directory_ / "active-session")
{
    fs::create_directories(sessions_directory_);
}

fs::path SessionStore::session_path(const std::string& id) const
{
    // IDs are generated internally and are deliberately restricted to a file name.
    if (id.empty() || id.find_first_of("/\\") != std::string::npos) return {};
    return sessions_directory_ / (id + ".json");
}

json SessionStore::to_json(const SessionData& data)
{
    json value = {
        {"version", 1},
        {"id", data.id},
        {"title", data.title},
        {"created_at", data.created_at},
        {"updated_at", data.updated_at},
        {"model", data.model},
        {"usage", {
            {"prompt_tokens", data.usage.prompt_tokens},
            {"completion_tokens", data.usage.completion_tokens},
            {"total_tokens", data.usage.total_tokens},
            {"cost", data.usage.cost}
        }},
        {"context_usage", data.context_usage},
        {"messages", json::array()}
    };
    for (const auto& message : data.history) {
        value["messages"].push_back(message_to_json(message));
    }
    return value;
}

std::optional<SessionData> SessionStore::from_json(const json& value)
{
    if (!value.is_object() || !value.contains("id")) return std::nullopt;
    SessionData data;
    data.id = value.value("id", "");
    if (data.id.empty()) return std::nullopt;
    data.title = value.value("title", "");
    data.created_at = value.value("created_at", "");
    data.updated_at = value.value("updated_at", "");
    data.model = value.value("model", "");
    data.context_usage = value.value("context_usage", 0);
    if (value.contains("usage") && value["usage"].is_object()) {
        const auto& usage = value["usage"];
        data.usage.prompt_tokens = usage.value("prompt_tokens", 0);
        data.usage.completion_tokens = usage.value("completion_tokens", 0);
        data.usage.total_tokens = usage.value("total_tokens", 0);
        if (usage.contains("cost") && usage["cost"].is_number()) {
            data.usage.cost = usage["cost"].get<double>();
        }
    }
    if (value.contains("messages") && value["messages"].is_array()) {
        for (const auto& message : value["messages"]) {
            if (message.is_object()) data.history.push_back(message_from_json(message));
        }
    }
    return data;
}

std::optional<SessionData> SessionStore::load(const std::string& id) const
{
    const auto path = session_path(id);
    if (path.empty()) return std::nullopt;
    std::ifstream file(path);
    if (!file) return std::nullopt;
    try {
        json value;
        file >> value;
        return from_json(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<SessionData> SessionStore::load_all() const
{
    std::vector<SessionData> sessions;
    if (!fs::exists(sessions_directory_)) return sessions;
    for (const auto& entry : fs::directory_iterator(sessions_directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto session = load(entry.path().stem().string());
        if (session) sessions.push_back(std::move(*session));
    }
    std::sort(sessions.begin(), sessions.end(), [](const auto& left, const auto& right) {
        return left.updated_at > right.updated_at;
    });
    return sessions;
}

bool SessionStore::save(const SessionData& data) const
{
    const auto path = session_path(data.id);
    if (path.empty()) return false;
    try {
        fs::create_directories(sessions_directory_);
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream file(temporary, std::ios::trunc);
            if (!file) return false;
            file << to_json(data).dump(2) << '\n';
        }
        std::error_code error;
        fs::remove(path, error);
        fs::rename(temporary, path, error);
        if (error) {
            fs::remove(temporary, error);
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool SessionStore::remove(const std::string& id) const
{
    const auto path = session_path(id);
    return !path.empty() && fs::remove(path);
}

std::string SessionStore::active_id() const
{
    std::ifstream file(active_path_);
    std::string id;
    std::getline(file, id);
    return id;
}

void SessionStore::set_active_id(const std::string& id) const
{
    fs::create_directories(directory_);
    std::ofstream file(active_path_, std::ios::trunc);
    if (file) file << id << '\n';
}

std::string SessionStore::new_id()
{
    static std::mt19937_64 generator(std::random_device{}());
    std::uniform_int_distribution<uint64_t> distribution;
    return "session-" + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()) +
        "-" + std::to_string(distribution(generator));
}
