#include "controller.h"

#include "tools/read.h"
#include "tools/write.h"
#include "tools/edit.h"
#include "tools/bash.h"
#include "tools/sub_agent.h"
#include "tools/search.h"
#include "tools/glob.h"
#include "tools/grep.h"
#include "providers/openrouter.h"
#include "project/context.h"

#include <algorithm>
#include <thread>

namespace {
constexpr int kDefaultBashTimeoutMs = 60 * 1000;
}

Controller::Controller()
    : config_(Config::load())
    , session_(config_)
    , stream_finish_reason_(FinishReason::Unknown)
{}

Controller::~Controller()
{
    tool_cancel_requested_.store(true);
    {
        std::lock_guard<std::mutex> lock(tool_worker_mutex_);
        if (tool_worker_.joinable()) {
            tool_worker_.join();
        }
    }
    if (session_.provider()) {
        session_.provider()->abort();
        session_.set_provider(nullptr);
    }
}

void Controller::setup_tools()
{
    tool_registry_.register_tool(std::make_unique<ReadTool>());
    tool_registry_.register_tool(std::make_unique<WriteTool>());
    tool_registry_.register_tool(std::make_unique<EditTool>());
    tool_registry_.register_tool(std::make_unique<BashTool>());
    tool_registry_.register_tool(std::make_unique<SubAgentTool>(
        [this]() { return config_; },
        [this]() { return session_.model(); }
    ));
    tool_registry_.register_tool(std::make_unique<SearchTool>(config_.exa));
    tool_registry_.register_tool(std::make_unique<GlobTool>());
    tool_registry_.register_tool(std::make_unique<GrepTool>());
}

void Controller::setup_commands()
{
    CommandInitializer::register_all(command_registry_);
}

void Controller::setup_provider()
{
    auto provider = std::make_unique<OpenRouterProvider>(config_.openrouter);
    provider->set_model(session_.model());
    model_catalog_ = provider->available_models();
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
}

void Controller::run_chat(const std::string& initial_prompt)
{
    setup_tools();
    setup_commands();
    setup_provider();
    build_tools_json();

    auto project_ctx = ProjectContext::discover();

    tui_ = std::make_unique<TuiApp>();

    tui_->sidebar().set_project_context(project_ctx);
    tui_->sidebar().set_model(session_.model());
    tui_->sidebar().set_token_count(0, 0);
    tui_->sidebar().set_context(session_.context_usage(), session_.provider()->context_window());
    tui_->chat_view().set_model(session_.model());

    tui_->status_bar().set_model(session_.model());
    tui_->status_bar().set_status("Ready");
    tui_->set_typing_state(false);

    tui_->input_bar().set_on_submit([this](const std::string& text) {
        handle_user_input(text);
    });

    tui_->input_bar().set_command_registry(&command_registry_);
    tui_->input_bar().set_models(model_catalog_);

    tui_->set_on_escape([this]() {
        handle_escape_key();
    });
    tui_->set_on_callback_error([this](std::string error) {
        handle_async_failure(error);
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
        const std::string previous_model = session_.model();
        CommandContext ctx{
            session_,
            tui_->chat_view(),
            &command_registry_,
            &tool_registry_,
            &skill_registry_,
            [this]() { tui_->request_refresh(); },
            [this](const std::string& key) { set_api_key(key); },
            [this](const std::string& key) { set_exa_api_key(key); }
        };
        command->execute(args, ctx);
        if (session_.model() != previous_model) {
            config_.openrouter.default_model = session_.model();
            config_.save();
        }
        tui_->status_bar().set_model(session_.model());
        tui_->sidebar().set_model(session_.model());
        tui_->chat_view().set_model(session_.model());
        tui_->sidebar().set_context(
            session_.context_usage(),
            session_.provider() ? session_.provider()->context_window() : 0
        );
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
    tool_cancel_requested_.store(false);

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
    tui_->set_typing_state(true);

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

    try {
        provider->send(
            session_.history(),
            tools_json_,
            session_.model(),
            [tui, this](Delta delta) {
                tui->post([this, delta = std::move(delta)]() { on_delta(delta); });
            },
            [tui, this](std::string error) {
                tui->post([this, error = std::move(error)]() { on_stream_error(error); });
            },
            [tui, this](Usage usage) {
                tui->post([this, usage]() { on_stream_done(usage); });
            }
        );
    } catch (const std::exception& e) {
        on_stream_error(std::string("Failed to start request: ") + e.what());
    } catch (...) {
        on_stream_error("Failed to start request");
    }
}

void Controller::on_delta(const Delta& delta)
{
    if (!processing_.load() || tool_cancel_requested_.load()) return;

    if (delta.content) {
        stream_content_ += *delta.content;
    }
    if (delta.tool_call) {
        int idx = delta.tool_call->index;
        auto& acc = stream_tool_calls_[idx];
        if (!delta.tool_call->id.empty()) acc.id = delta.tool_call->id;
        if (!delta.tool_call->type.empty()) acc.type = delta.tool_call->type;
        if (!delta.tool_call->function_name.empty()) acc.function_name = delta.tool_call->function_name;
        acc.arguments = delta.tool_call->arguments;
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
    const bool was_aborted = abort_pending_.load() || tool_cancel_requested_.load();
    if (was_aborted) {
        tui_->chat_view().show_system_message("Response aborted.");
    } else {
        tui_->chat_view().show_system_message("Error: " + error);
    }
    tui_->status_bar().set_status(was_aborted ? "Aborted" : "Error");
    tui_->set_typing_state(false);
    processing_.store(false);
    abort_pending_.store(false);
    tool_cancel_requested_.store(false);
}

void Controller::on_stream_done(Usage usage)
{
    if (!processing_.load() || tool_cancel_requested_.load()) return;

    session_.add_usage(usage);
    tui_->status_bar().set_token_count(
        session_.total_usage().prompt_tokens,
        session_.total_usage().completion_tokens
    );
    tui_->sidebar().set_token_count(
        session_.total_usage().prompt_tokens,
        session_.total_usage().completion_tokens
    );
    session_.set_context_usage(
        usage.prompt_tokens > 0
            ? usage.prompt_tokens
            : session_.estimate_context_usage(*session_.provider())
    );
    tui_->sidebar().set_context(session_.context_usage(), session_.provider()->context_window());

    // Build the final assistant message from accumulated data
    Message assistant_msg;
    assistant_msg.role = MessageRole::Assistant;
    assistant_msg.content = stream_content_;

    // Extract tool calls into a local before any move operations
    auto tool_calls_to_process = std::move(stream_tool_calls_);
    stream_tool_calls_.clear();
    for (const auto& [idx, tc] : tool_calls_to_process) {
        assistant_msg.tool_calls.push_back(tc);
    }
    session_.add_message(assistant_msg);

    // Check if we need to execute tool calls
    if (stream_finish_reason_ == FinishReason::ToolCalls && !tool_calls_to_process.empty()) {
        execute_tool_calls_and_continue(std::move(tool_calls_to_process));
    } else {
        tui_->status_bar().set_status("Ready");
        tui_->set_typing_state(false);
        processing_.store(false);
    }
}

void Controller::execute_tool_calls_and_continue(std::map<int, ToolCall> tool_calls)
{
    // Keep filesystem, regex, and process tools off the UI event loop. This
    // also prevents a tool callback from re-entering the provider lifecycle.
    tui_->status_bar().set_status("Running tools...");
    tui_->set_typing_state(true);
    tool_execution_active_.store(true);

    try {
        std::lock_guard<std::mutex> lock(tool_worker_mutex_);
        if (tool_worker_.joinable()) {
            if (tool_worker_.get_id() == std::this_thread::get_id()) {
                throw std::runtime_error("Tool worker attempted to replace itself");
            }
            tool_worker_.join();
        }

        tool_worker_ = std::thread([this, tool_calls = std::move(tool_calls)]() mutable noexcept {
            try {
                std::vector<ToolExecutionResult> results;
                results.reserve(tool_calls.size());

                for (auto& [idx, tc] : tool_calls) {
                    (void)idx;
                    ToolExecutionResult execution;
                    execution.call = tc;

                    if (tool_cancel_requested_.load()) {
                        execution.output = "Tool cancelled by user";
                        results.push_back(std::move(execution));
                        continue;
                    }

                    if (tc.function_name.empty()) {
                        execution.output = "tool name was empty";
                        results.push_back(std::move(execution));
                        continue;
                    }

                    Tool* tool = tool_registry_.find(tc.function_name);

                    if (!tool) {
                        execution.output = "Unknown tool '" + tc.function_name + "'";
                    } else {
                        try {
                            json args = json::parse(tc.arguments);
                            execution.arguments = args;

                            ToolOutputCallback on_output;
                            if (tc.function_name == "bash") {
                                execution.display_key = "bash-" +
                                    std::to_string(tool_display_sequence_.fetch_add(1));
                                int timeout = args.value("timeout", kDefaultBashTimeoutMs);
                                if (timeout <= 0) timeout = kDefaultBashTimeoutMs;
                                const bool explicit_timeout = args.contains("timeout") &&
                                    args["timeout"].is_number_integer() &&
                                    args["timeout"].get<int>() > 0;
                                const std::string timeout_label = std::to_string(timeout) +
                                    "ms" + (explicit_timeout ? "" : " (default)");
                                const std::string key = execution.display_key;
                                const std::string command = args.value("command", "");
                                tui_->post([this, key, command, timeout_label]() {
                                    tui_->chat_view().show_bash_started(key, command, timeout_label);
                                });
                                on_output = [this, key](const std::string& output) {
                                    if (output.empty()) return;
                                    tui_->post([this, key, output]() {
                                        tui_->chat_view().append_bash_output(key, output);
                                    });
                                };
                            } else if (tc.function_name == "search") {
                                execution.display_key = "search-" +
                                    std::to_string(tool_display_sequence_.fetch_add(1));
                                const std::string key = execution.display_key;
                                const std::string query = args.value("query", "");
                                tui_->post([this, key, query]() {
                                    tui_->chat_view().show_bash_started(
                                        key, query, "Exa", "search");
                                });
                            } else if (tc.function_name == "sub_agent") {
                                execution.display_key = "sub-agent-" +
                                    std::to_string(tool_display_sequence_.fetch_add(1));
                                const std::string key = execution.display_key;
                                const std::string task = args.value("task", "");
                                const std::string mode = args.value("mode", "R");
                                tui_->post([this, key, task, mode]() {
                                    tui_->chat_view().show_subagent_started(key, task, mode);
                                });
                                on_output = [this, key](const std::string& event) {
                                    tui_->post([this, key, event]() {
                                        tui_->chat_view().update_subagent(key, event);
                                    });
                                };
                            }

                            ToolCancelCallback should_cancel = [this]() {
                                return tool_cancel_requested_.load();
                            };
                            ToolResult result = tool->execute_stream(
                                args, std::move(on_output), std::move(should_cancel));
                            execution.success = result.success;
                            execution.output = std::move(result.output);
                            execution.data = std::move(result.data);
                        } catch (const json::exception& e) {
                            execution.output = std::string("Invalid tool arguments: ") + e.what();
                        } catch (const std::exception& e) {
                            execution.output = std::string("Tool failed: ") + e.what();
                        } catch (...) {
                            execution.output = "Tool failed with an unknown error";
                        }
                    }
                    results.push_back(std::move(execution));
                }

                tui_->post([this, results = std::move(results)]() mutable {
                    finish_tool_execution(std::move(results));
                });
            } catch (const std::exception& e) {
                try {
                    std::string error = std::string("Tool worker failed: ") + e.what();
                    tui_->post([this, error = std::move(error)]() {
                        handle_async_failure(error);
                    });
                } catch (...) {
                }
            } catch (...) {
                try {
                    tui_->post([this]() {
                        handle_async_failure("Tool worker failed with an unknown error");
                    });
                } catch (...) {
                }
            }
        });
    } catch (const std::exception& e) {
        tool_execution_active_.store(false);
        tui_->chat_view().show_system_message("Tool execution failed: " + std::string(e.what()));
        tui_->status_bar().set_status("Error");
        tui_->set_typing_state(false);
        processing_.store(false);
    }
}

void Controller::handle_async_failure(const std::string& error) noexcept
{
    processing_.store(false);
    abort_pending_.store(false);
    tool_cancel_requested_.store(false);
    tool_execution_active_.store(false);
    try {
        if (auto* provider = session_.provider()) {
            provider->abort();
        }
        tui_->set_typing_state(false);
        tui_->status_bar().set_status("Error");
        tui_->chat_view().show_system_message("Internal error: " + error);
        tui_->request_refresh();
    } catch (...) {
        // Keep the TUI alive even if rendering the diagnostic itself fails.
    }
}

void Controller::finish_tool_execution(std::vector<ToolExecutionResult> results)
{
    const bool cancelled = tool_cancel_requested_.load();
    tool_execution_active_.store(false);

    const auto bash_succeeded = [](const ToolExecutionResult& execution) {
        if (!execution.data.is_object() ||
            !execution.data.contains("exit_code") ||
            !execution.data["exit_code"].is_number_integer()) {
            return false;
        }

        const int exit_code = execution.data["exit_code"].get<int>();
        const bool timed_out = execution.data.value("timed_out", false);
        const bool was_cancelled = execution.data.value("cancelled", false);
        return exit_code == 0 && !timed_out && !was_cancelled;
    };

    for (const auto& execution : results) {
        const auto& tc = execution.call;
        Message result_msg;
        result_msg.role = MessageRole::Tool;
        result_msg.content = execution.success ? execution.output : "Error: " + execution.output;
        if (!tc.id.empty()) {
            result_msg.tool_call_id = tc.id;
        }
        if (!tc.function_name.empty()) {
            result_msg.name = tc.function_name;
        }
        session_.add_message(result_msg);

        if (tc.function_name == "read" || tc.function_name == "glob" || tc.function_name == "grep") {
            std::string summary;
            if (!execution.success) {
                summary = tc.function_name + " failed";
            } else if (tc.function_name == "read") {
                summary = "Read " + execution.arguments.value("path", "file");
            } else if (execution.output.rfind("No files matching", 0) == 0 ||
                       execution.output.rfind("No matches found", 0) == 0) {
                summary = tc.function_name + ": no matches";
            } else {
                const auto line_count = static_cast<int>(std::count(
                    execution.output.begin(), execution.output.end(), '\n')) +
                    (!execution.output.empty() && execution.output.back() != '\n' ? 1 : 0);
                if (tc.function_name == "grep") {
                    summary = "Grep \"" + execution.arguments.value("pattern", "") + "\" · " +
                        std::to_string(line_count) + " match" + (line_count == 1 ? "" : "es");
                } else {
                    summary = "Glob \"" + execution.arguments.value("pattern", "") + "\" · " +
                        std::to_string(line_count) + " result" + (line_count == 1 ? "" : "s");
                }
            }
            tui_->chat_view().show_tool_activity(tc.function_name, summary);
        } else if (tc.function_name == "bash" || tc.function_name == "search") {
            const bool succeeded = tc.function_name == "bash"
                ? bash_succeeded(execution)
                : execution.success;
            if (!execution.display_key.empty()) {
                tui_->chat_view().finish_bash(
                    execution.display_key, execution.output, succeeded);
            } else {
                tui_->chat_view().show_system_message(
                    tc.function_name + " : command : " + std::to_string(kDefaultBashTimeoutMs) +
                    "ms : " + execution.output);
            }
        } else if (tc.function_name == "sub_agent") {
            if (!execution.display_key.empty()) {
                tui_->chat_view().finish_subagent(
                    execution.display_key, execution.output, execution.success);
            } else {
                tui_->chat_view().show_system_message(
                    "sub_agent : " + execution.output);
            }
        } else if ((tc.function_name == "write" || tc.function_name == "edit") &&
                   execution.success && execution.data.is_object() &&
                   execution.data.contains("path") && execution.data.contains("before") &&
                   execution.data.contains("after")) {
            tui_->chat_view().show_tool_diff(
                tc.function_name,
                execution.data.value("path", tc.function_name),
                execution.data.value("before", ""),
                execution.data.value("after", ""));
        } else {
            tui_->chat_view().add_message(result_msg);
        }
    }

    stream_content_.clear();
    stream_tool_calls_.clear();
    stream_finish_reason_ = FinishReason::Unknown;

    if (cancelled) {
        tui_->status_bar().set_status("Aborted");
        tui_->set_typing_state(false);
        processing_.store(false);
        abort_pending_.store(false);
        tool_cancel_requested_.store(false);
        tui_->request_refresh();
        return;
    }

    tui_->status_bar().set_status("Thinking...");
    tui_->set_typing_state(true);
    send_to_provider();
}

void Controller::handle_escape_key()
{
    if (!processing_.load()) {
        tui_->stop();
        return;
    }

    if (tool_cancel_requested_.load()) {
        return;
    }

    if (abort_pending_.load()) {
        // Second press: actually abort
        tool_cancel_requested_.store(true);
        auto* provider = session_.provider();
        if (provider) {
            provider->abort();
        }
        tui_->status_bar().set_status("Aborting...");
        tui_->request_refresh();
        abort_pending_.store(false);
        // Keep processing_ true until the provider/tool worker posts its
        // completion. This prevents a new request from racing stale callbacks.
        if (!tool_execution_active_.load()) {
            tui_->set_typing_state(true);
        }
    } else {
        // First press: show pending
        abort_pending_.store(true);
        last_abort_press_ = std::chrono::steady_clock::now();
        tui_->status_bar().set_status("Press Esc again to abort");
        tui_->request_refresh();
    }
}

void Controller::reset_abort_pending()
{
    if (abort_pending_.load()) {
        abort_pending_.store(false);
        tool_cancel_requested_.store(false);
        if (processing_.load()) {
            tui_->status_bar().set_status("Thinking...");
            tui_->request_refresh();
        }
    }
}

void Controller::set_api_key(const std::string& api_key)
{
    config_.openrouter.api_key = api_key;
    config_.save();

    auto* provider = session_.provider();
    if (!provider) {
        return;
    }

    provider->set_api_key(api_key);
    provider->set_model(session_.model());
    model_catalog_ = provider->available_models();
    tui_->input_bar().set_models(model_catalog_);
    tui_->sidebar().set_context(session_.context_usage(), provider->context_window());
    tui_->request_refresh();
}

void Controller::set_exa_api_key(const std::string& api_key)
{
    config_.exa.api_key = api_key;
    config_.save();

    auto* search_tool = dynamic_cast<SearchTool*>(tool_registry_.find("search"));
    if (search_tool) {
        search_tool->set_api_key(api_key);
    }

    tui_->request_refresh();
}
