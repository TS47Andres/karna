#include "controller.h"

#include "tools/read.h"
#include "tools/write.h"
#include "tools/edit.h"
#include "tools/run.h"
#include "tools/search.h"
#include "tools/glob.h"
#include "tools/grep.h"
#include "providers/openrouter.h"

#include <sstream>
#include <thread>

Controller::Controller()
    : config_(Config::load())
    , session_(config_)
{}

Controller::~Controller() = default;

void Controller::setup_tools()
{
    tool_registry_.register_tool(std::make_unique<ReadTool>());
    tool_registry_.register_tool(std::make_unique<WriteTool>());
    tool_registry_.register_tool(std::make_unique<EditTool>());
    tool_registry_.register_tool(std::make_unique<RunTool>());
    tool_registry_.register_tool(std::make_unique<SearchTool>(config_.exa));
    tool_registry_.register_tool(std::make_unique<GlobTool>());
    tool_registry_.register_tool(std::make_unique<GrepTool>());
}

void Controller::setup_skills()
{
    SkillInitializer::register_all(skill_registry_);
}

void Controller::setup_commands()
{
    CommandInitializer::register_all(command_registry_);
}

void Controller::setup_provider()
{
    auto provider = std::make_unique<OpenRouterProvider>(config_.openrouter);
    session_.set_provider(std::move(provider));
}

void Controller::run_chat(const std::string& initial_prompt)
{
    setup_tools();
    setup_skills();
    setup_commands();
    setup_provider();

    tui_ = std::make_unique<TuiApp>();

    tui_->status_bar().set_model(session_.model());
    tui_->status_bar().set_status("Ready");
    tui_->status_bar().set_typing(false);

    tui_->chat_view().show_system_message(
        "Karna v0.1.0 | Model: " + session_.model() +
        " | Type /help for commands"
    );

    tui_->input_bar().set_on_submit([this](const std::string& text) {
        handle_user_input(text);
    });

    if (!initial_prompt.empty()) {
        handle_user_input(initial_prompt);
    }

    tui_->run();
}

void Controller::handle_user_input(const std::string& text)
{
    if (processing_) return;

    if (text.empty()) return;

    if (text[0] == '/') {
        size_t space = text.find(' ');
        std::string cmd = (space == std::string::npos) ? text.substr(1) : text.substr(1, space - 1);
        std::string args = (space == std::string::npos) ? "" : text.substr(space + 1);
        handle_slash_command(cmd, args);
    } else {
        handle_normal_message(text);
    }
}

void Controller::handle_slash_command(const std::string& cmd, const std::string& args)
{
    if (cmd == "help") {
        show_help();
        return;
    }
    if (cmd == "skills") {
        show_skills();
        return;
    }
    if (cmd == "session") {
        show_session_info();
        return;
    }

    auto* command = command_registry_.find(cmd);
    if (command) {
        CommandContext ctx{session_, tui_->chat_view(), [this]() { tui_->request_refresh(); }};
        command->execute(args, ctx);
        tui_->request_refresh();
    } else {
        tui_->chat_view().show_system_message("Unknown command: /" + cmd + ". Type /help for available commands.");
        tui_->request_refresh();
    }
}

void Controller::handle_normal_message(const std::string& text)
{
    processing_ = true;

    Message user_msg;
    user_msg.role = MessageRole::User;
    user_msg.content = text;
    session_.add_message(user_msg);
    tui_->chat_view().add_message(user_msg);

    Message assistant_msg;
    assistant_msg.role = MessageRole::Assistant;
    session_.add_message(assistant_msg);

    tui_->status_bar().set_status("Thinking...");
    tui_->status_bar().set_typing(true);
    tui_->request_refresh();

    send_to_provider();
}

void Controller::send_to_provider()
{
    auto* provider = session_.provider();
    if (!provider) {
        tui_->chat_view().show_system_message("Error: No provider configured.");
        processing_ = false;
        return;
    }

    auto tool_names = tool_registry_.all_names();

    // Build tool schemas for the provider
    json tools_json = json::array();
    for (const auto* t : tool_registry_.all()) {
        json tool;
        tool["type"] = "function";
        tool["function"] = json::object();
        tool["function"]["name"] = t->name();
        tool["function"]["description"] = t->description();
        tool["function"]["parameters"] = t->parameters();
        tools_json.push_back(tool);
    }
    // Add skills as tools
    for (const auto* s : skill_registry_.all()) {
        json tool;
        tool["type"] = "function";
        tool["function"] = json::object();
        tool["function"]["name"] = s->name();
        tool["function"]["description"] = s->description();
        tool["function"]["parameters"] = s->parameters();
        tools_json.push_back(tool);
    }

    auto* tui = tui_.get();

    provider->send(
        session_.history(),
        tool_names,
        // on_delta
        [tui, this](Delta delta) {
            tui->chat_view().add_delta(delta);
            tui->request_refresh();
        },
        // on_error
        [tui, this](std::string error) {
            tui->chat_view().show_system_message("Error: " + error);
            tui->status_bar().set_status("Error");
            tui->status_bar().set_typing(false);
            tui->request_refresh();
            processing_ = false;
        },
        // on_done
        [tui, this](Usage usage) {
            session_.add_usage(usage);
            tui->status_bar().set_token_count(
                session_.total_usage().prompt_tokens,
                session_.total_usage().completion_tokens
            );
            tui->status_bar().set_status("Ready");
            tui->status_bar().set_typing(false);
            tui->request_refresh();
            processing_ = false;
        }
    );
}

void Controller::show_help()
{
    std::ostringstream help;
    help << "Available commands:\n";
    for (const auto* cmd : command_registry_.all()) {
        help << "  /" << cmd->name() << " - " << cmd->description() << "\n";
    }
    help << "\nAvailable tools:\n";
    for (const auto* t : tool_registry_.all()) {
        help << "  " << t->name() << " - " << t->description() << "\n";
    }
    help << "\nAvailable skills:\n";
    for (const auto* s : skill_registry_.all()) {
        help << "  /" << s->name() << " - " << s->description() << "\n";
    }
    tui_->chat_view().show_system_message(help.str());
    tui_->request_refresh();
}

void Controller::show_skills()
{
    std::ostringstream skills;
    skills << "Available skills:\n";
    for (const auto* s : skill_registry_.all()) {
        skills << "  " << s->name() << " - " << s->description() << "\n";
    }
    tui_->chat_view().show_system_message(skills.str());
    tui_->request_refresh();
}

void Controller::show_session_info()
{
    std::ostringstream info;
    info << "Session info:\n";
    info << "  Model: " << session_.model() << "\n";
    info << "  Messages: " << session_.history().size() << "\n";
    info << "  Tokens: " << session_.total_usage().total_tokens
         << " (P:" << session_.total_usage().prompt_tokens
         << " C:" << session_.total_usage().completion_tokens << ")\n";
    tui_->chat_view().show_system_message(info.str());
    tui_->request_refresh();
}
