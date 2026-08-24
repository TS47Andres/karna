#pragma once

#include <memory>
#include <functional>
#include <atomic>
#include <map>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <cstdint>
#include <set>

#include "config/config.h"
#include "core/session.h"
#include "core/session_store.h"
#include "core/provider.h"
#include "core/message.h"
#include "core/tool.h"
#include "commands/registry.h"
#include "tui/app.h"

class Controller {
public:
    Controller();
    ~Controller();

    void run_chat(const std::string& initial_prompt = "");

private:
    struct SessionRuntime {
        SessionRuntime(const Config& config, std::string id, std::string title)
            : session(config, std::move(id), std::move(title)) {}

        Session session;
        std::atomic<bool> processing{false};
        std::atomic<bool> abort_pending{false};
        std::atomic<bool> tool_cancel_requested{false};
        std::atomic<bool> tool_execution_active{false};
        std::atomic<uint64_t> tool_display_sequence{0};
        std::thread tool_worker;
        std::mutex tool_worker_mutex;
        std::string stream_content;
        std::map<int, ToolCall> stream_tool_calls;
        FinishReason stream_finish_reason{FinishReason::Unknown};
    };

    Config config_;
    SessionStore session_store_;
    mutable std::mutex sessions_mutex_;
    std::map<std::string, std::unique_ptr<SessionRuntime>> sessions_;
    std::string active_session_id_;
    std::set<std::string> persistence_failures_;
    ToolRegistry tool_registry_;
    CommandRegistry command_registry_;
    std::unique_ptr<TuiApp> tui_;

    std::chrono::steady_clock::time_point last_abort_press_;

    json tools_json_;
    std::vector<ModelInfo> model_catalog_;

    struct ToolExecutionResult {
        ToolCall call;
        std::string output;
        bool success{false};
        json arguments{json::object()};
        json data{json::object()};
        std::string display_key;
        std::string display_transcript;
    };

    SessionRuntime* runtime(const std::string& id) const;
    SessionRuntime& active_runtime();
    void load_sessions();
    SessionRuntime& create_session();
    void persist(SessionRuntime& runtime);
    void refresh_session_picker();
    void refresh_active_view();
    void update_active_ui();
    void switch_session(const std::string& id);
    void delete_current_session();
    void show_sessions();
    void setup_tools();
    void setup_commands();
    void setup_provider(SessionRuntime& runtime, bool load_model_catalog = false);

    void handle_user_input(const std::string& text);
    void handle_slash_command(const std::string& cmd, const std::string& args);
    void handle_normal_message(const std::string& text);

    void send_to_provider(const std::string& session_id);
    void build_tools_json();
    void on_delta(const std::string& session_id, const Delta& delta);
    void on_stream_error(const std::string& session_id, const std::string& error);
    void on_stream_done(const std::string& session_id, Usage usage);
    void execute_tool_calls_and_continue(const std::string& session_id, std::map<int, ToolCall> tool_calls);
    void finish_tool_execution(const std::string& session_id, std::vector<ToolExecutionResult> results);
    void handle_async_failure(const std::string& session_id, const std::string& error) noexcept;

    void handle_escape_key();
    void reset_abort_pending();
    void set_api_key(const std::string& api_key);
    void set_exa_api_key(const std::string& api_key);
};
