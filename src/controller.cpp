#include "controller.h"

#include "tools/read.h"
#include "tools/write.h"
#include "tools/edit.h"
#include "tools/run.h"
#include "tools/search.h"
#include "tools/glob.h"
#include "tools/grep.h"
#include "providers/openrouter.h"

#include <thread>

Controller::Controller()
    : config_(Config::load())
    , session_(config_)
    , stream_finish_reason_(FinishReason::Unknown)
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

void Controller::build_tools_json()
{
    tools_json_ = json::array();
    for (const auto* t : tool_registry_.all()) {
        json tool;
        tool["type"] = "function";
        tool["function"] = json::object();
        tool["function"]["name"] = t->name();
        tool["function"]["description"] = t->description();
        tool["function"]["parameters"] = t->parameters();
        tools_json_.push_back(tool);
    }
    for (const auto* s : skill_registry_.all()) {
        json tool;
        tool["type"] = "function";
        tool["function"] = json::object();
        tool["function"]["name"] = s->name();
        tool["function"]["description"] = s->description();
        tool["function"]["parameters"] = s->parameters();
        tools_json_.push_back(tool);
    }
}

void Controller::run_chat(const std::string& initial_prompt)
{
    setup_tools();
    setup_skills();
    setup_commands();
    setup_provider();
    build_tools_json();

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

    tui_->set_on_escape([this]() {
        handle_escape_key();
    });

    if (!initial_prompt.empty()) {
        handle_user_input(initial_prompt);
    }

    tui_->run();
}

void Controller::handle_user_input(const std::string& text)
{
    if (processing_.load()) return;
    if (text.empty()) return;

    reset_abort_pending();

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
    auto* command = command_registry_.find(cmd);
    if (command) {
        CommandContext ctx{session_, tui_->chat_view(), &command_registry_, &tool_registry_, &skill_registry_, [this]() { tui_->request_refresh(); }};
        command->execute(args, ctx);
        tui_->request_refresh();
    } else {
        tui_->chat_view().show_system_message("Unknown command: /" + cmd + ". Type /help for available commands.");
        tui_->request_refresh();
    }
}

void Controller::handle_normal_message(const std::string& text)
{
    processing_.store(true);
    abort_pending_.store(false);

    Message user_msg;
    user_msg.role = MessageRole::User;
    user_msg.content = text;
    session_.add_message(user_msg);
    tui_->chat_view().add_message(user_msg);

    // Clear stream accumulators
    stream_content_.clear();
    stream_tool_calls_.clear();
    stream_finish_reason_ = FinishReason::Unknown;

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
        processing_.store(false);
        return;
    }

    auto* tui = tui_.get();

    provider->send(
        session_.history(),
        tools_json_,
        [tui, this](Delta delta) { this->on_delta(delta); },
        [tui, this](std::string error) { this->on_stream_error(error); },
        [tui, this](Usage usage) { this->on_stream_done(usage); }
    );
}

void Controller::on_delta(const Delta& delta)
{
    if (delta.content) {
        stream_content_ += *delta.content;
    }
    if (delta.tool_call) {
        int idx = delta.tool_call->index;
        auto& acc = stream_tool_calls_[idx];
        if (!delta.tool_call->id.empty()) acc.id = delta.tool_call->id;
        if (!delta.tool_call->type.empty()) acc.type = delta.tool_call->type;
        if (!delta.tool_call->function_name.empty()) acc.function_name = delta.tool_call->function_name;
        acc.arguments += delta.tool_call->arguments;
        acc.index = idx;
    }
    if (delta.finish_reason) {
        stream_finish_reason_ = *delta.finish_reason;
    }
    tui_->chat_view().add_delta(delta);
    tui_->request_refresh();
}

void Controller::on_stream_error(const std::string& error)
{
    if (abort_pending_.load() || abort_pending_.load()) {
        tui_->chat_view().show_system_message("Response aborted.");
    } else {
        tui_->chat_view().show_system_message("Error: " + error);
    }
    tui_->status_bar().set_status(abort_pending_.load() ? "Aborted" : "Error");
    tui_->status_bar().set_typing(false);
    tui_->request_refresh();
    processing_.store(false);
    abort_pending_.store(false);
}

void Controller::on_stream_done(Usage usage)
{
    session_.add_usage(usage);
    tui_->status_bar().set_token_count(
        session_.total_usage().prompt_tokens,
        session_.total_usage().completion_tokens
    );

    // Build the final assistant message from accumulated data
    Message assistant_msg;
    assistant_msg.role = MessageRole::Assistant;
    assistant_msg.content = stream_content_;
    for (auto& [idx, tc] : stream_tool_calls_) {
        assistant_msg.tool_calls.push_back(std::move(tc));
    }
    session_.add_message(assistant_msg);

    // Check if we need to execute tool calls
    if (stream_finish_reason_ == FinishReason::ToolCalls && !stream_tool_calls_.empty()) {
        tui_->status_bar().set_status("Running tools...");
        tui_->status_bar().set_typing(true);
        tui_->request_refresh();
        execute_tool_calls_and_continue();
    } else {
        tui_->status_bar().set_status("Ready");
        tui_->status_bar().set_typing(false);
        tui_->request_refresh();
        processing_.store(false);
    }
}

void Controller::execute_tool_calls_and_continue()
{
    // Execute each tool call
    for (auto& [idx, tc] : stream_tool_calls_) {
        Tool* tool = tool_registry_.find(tc.function_name);
        if (!tool) {
            Skill* skill = skill_registry_.find(tc.function_name);
            if (skill) {
                tool = skill;
            }
        }

        std::string result_content;
        if (!tool) {
            result_content = "Error: Unknown tool '" + tc.function_name + "'";
        } else {
            try {
                json args = json::parse(tc.arguments);
                ToolResult result = tool->execute(args);
                result_content = result.output;
                if (!result.success) {
                    result_content = "Error: " + result.output;
                }
            } catch (const std::exception& e) {
                result_content = std::string("Error parsing arguments: ") + e.what();
            }
        }

        tui_->chat_view().show_system_message(
            "Tool '" + tc.function_name + "' executed"
        );

        // Add tool result to session
        Message result_msg;
        result_msg.role = MessageRole::Tool;
        result_msg.content = result_content;
        result_msg.tool_call_id = tc.id;
        session_.add_message(result_msg);
    }

    // Clear accumulators for the next round
    stream_content_.clear();
    stream_tool_calls_.clear();
    stream_finish_reason_ = FinishReason::Unknown;

    tui_->status_bar().set_status("Thinking...");
    tui_->status_bar().set_typing(true);
    tui_->request_refresh();

    send_to_provider();
}

void Controller::handle_escape_key()
{
    if (!processing_.load()) return;

    if (abort_pending_.load()) {
        // Second press: actually abort
        auto* provider = session_.provider();
        if (provider) {
            provider->abort();
        }
        tui_->status_bar().set_status("Aborting...");
        tui_->request_refresh();
        abort_pending_.store(false);
        processing_.store(false);
    } else {
        // First press: show pending
        abort_pending_.store(true);
        last_abort_press_ = std::chrono::steady_clock::now();
        tui_->status_bar().set_status("Press F5 again to abort");
        tui_->request_refresh();
    }
}

void Controller::reset_abort_pending()
{
    if (abort_pending_.load()) {
        abort_pending_.store(false);
        if (processing_.load()) {
            tui_->status_bar().set_status("Thinking...");
            tui_->request_refresh();
        }
    }
}


