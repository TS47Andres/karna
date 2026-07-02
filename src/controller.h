#pragma once

#include <memory>
#include <functional>

#include "config/config.h"
#include "core/session.h"
#include "core/provider.h"
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

    bool processing_{false};

    void setup_tools();
    void setup_skills();
    void setup_commands();
    void setup_provider();

    void handle_user_input(const std::string& text);
    void handle_slash_command(const std::string& cmd, const std::string& args);
    void handle_normal_message(const std::string& text);

    void send_to_provider();
    void process_tool_calls(const std::vector<Message>& response_messages);
    void send_tool_results(const std::vector<Message>& tool_results);

    void show_help();
    void show_skills();
    void show_session_info();
};
