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

#include "config/config.h"
#include "core/session.h"
#include "core/provider.h"
#include "core/message.h"
#include "core/tool.h"
#include "tools/registry.h"
#include "skills/registry.h"
#include "commands/registry.h"
#include "tui/app.h"

class Controller {
public:
    Controller();
    ~Controller();

    void run_chat(const std::string& initial_prompt = "");

private:
    Config config_;
    Session session_;
    ToolRegistry tool_registry_;
    SkillRegistry skill_registry_;
    CommandRegistry command_registry_;
    std::unique_ptr<TuiApp> tui_;

    std::atomic<bool> processing_{false};
    std::atomic<bool> abort_pending_{false};
    std::atomic<bool> tool_cancel_requested_{false};
    std::chrono::steady_clock::time_point last_abort_press_;

    json tools_json_;
    std::vector<ModelInfo> model_catalog_;

    // Per-stream accumulators
    std::string stream_content_;
    std::map<int, ToolCall> stream_tool_calls_;
    FinishReason stream_finish_reason_;

    struct ToolExecutionResult {
        ToolCall call;
        std::string output;
        bool success{false};
        json arguments{json::object()};
        json data{json::object()};
        std::string display_key;
    };
    std::mutex tool_worker_mutex_;
    std::thread tool_worker_;

    void setup_tools();
    void setup_skills();
    void setup_commands();
    void setup_provider();

    void handle_user_input(const std::string& text);
    void handle_slash_command(const std::string& cmd, const std::string& args);
    void handle_normal_message(const std::string& text);

    void send_to_provider();
    void build_tools_json();
    void on_delta(const Delta& delta);
    void on_stream_error(const std::string& error);
    void on_stream_done(Usage usage);
    void execute_tool_calls_and_continue(std::map<int, ToolCall> tool_calls);
    void finish_tool_execution(std::vector<ToolExecutionResult> results);
    void handle_async_failure(const std::string& error) noexcept;

    void handle_escape_key();
    void reset_abort_pending();
    void set_api_key(const std::string& api_key);
};
