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
#include <cctype>
#include <sstream>
#include <thread>

namespace {
constexpr int kDefaultBashTimeoutMs = 60 * 1000;
}

Controller::Controller()
    : config_(Config::load())
{
    load_sessions();
}

Controller::~Controller()
{
    for (auto& [id, runtime] : sessions_) {
        runtime->tool_cancel_requested.store(true);
        if (runtime->session.provider()) {
            runtime->session.provider()->abort();
        }
        std::lock_guard<std::mutex> lock(runtime->tool_worker_mutex);
        if (runtime->tool_worker.joinable()) {
            runtime->tool_worker.join();
        }
    }
}

Controller::SessionRuntime* Controller::runtime(const std::string& id) const
{
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second.get();
}

Controller::SessionRuntime& Controller::active_runtime()
{
    return *sessions_.at(active_session_id_);
}

void Controller::load_sessions()
{
    for (const auto& data : session_store_.load_all()) {
        auto session = std::make_unique<SessionRuntime>(config_, data.id, data.title);
        session->session.restore(data);
        persist(*session);
        sessions_.emplace(data.id, std::move(session));
    }

    const auto saved_active = session_store_.active_id();
    if (!saved_active.empty() && runtime(saved_active)) {
        active_session_id_ = saved_active;
    } else if (!sessions_.empty()) {
        active_session_id_ = sessions_.begin()->first;
    } else {
        create_session();
    }
    session_store_.set_active_id(active_session_id_);
}

Controller::SessionRuntime& Controller::create_session()
{
    const std::string id = SessionStore::new_id();
    auto session = std::make_unique<SessionRuntime>(config_, id, "New session");
    auto* result = session.get();
    sessions_.emplace(id, std::move(session));
    active_session_id_ = id;
    persist(*result);
    session_store_.set_active_id(id);
    return *result;
}

void Controller::persist(SessionRuntime& runtime)
{
    session_store_.save(runtime.session.snapshot());
    refresh_session_picker();
}

void Controller::refresh_session_picker()
{
    if (!tui_) return;

    std::vector<InputBar::SessionChoice> choices;
    for (const auto& data : session_store_.load_all()) {
        const auto* current = runtime(data.id);
        choices.push_back({
            data.id,
            current ? current->session.title() : data.title,
            data.id == active_session_id_,
            current && current->processing.load()
        });
    }
    tui_->input_bar().set_sessions(choices);
}

void Controller::refresh_active_view()
{
    auto& current = active_runtime();
    tui_->chat_view().load_history(current.session.history());
    if (current.processing.load() &&
        (!current.stream_content.empty() || !current.stream_tool_calls.empty())) {
        Message partial;
        partial.role = MessageRole::Assistant;
        partial.content = current.stream_content;
        for (const auto& [index, call] : current.stream_tool_calls) {
            (void)index;
            partial.tool_calls.push_back(call);
        }
        tui_->chat_view().add_message(partial);
    }
}

void Controller::update_active_ui()
{
    auto& current = active_runtime();
    tui_->status_bar().set_model(current.session.model());
    tui_->sidebar().set_model(current.session.model());
    tui_->chat_view().set_model(current.session.model());
    tui_->status_bar().set_token_count(
        current.session.total_usage().prompt_tokens,
        current.session.total_usage().completion_tokens);
    tui_->sidebar().set_token_count(
        current.session.total_usage().prompt_tokens,
        current.session.total_usage().completion_tokens);
    tui_->sidebar().set_context(
        current.session.context_usage(),
        current.session.provider() ? current.session.provider()->context_window() : 0);
    tui_->set_typing_state(current.processing.load());
    tui_->status_bar().set_status(current.processing.load() ? "Thinking..." : "Ready");
}

void Controller::switch_session(const std::string& id)
{
    if (!runtime(id)) {
        if (const auto data = session_store_.load(id)) {
            auto session = std::make_unique<SessionRuntime>(config_, data->id, data->title);
            session->session.restore(*data);
            sessions_.emplace(data->id, std::move(session));
        }
    }
    if (!runtime(id)) {
        tui_->chat_view().show_system_message("Unknown session: " + id);
        return;
    }
    active_session_id_ = id;
    session_store_.set_active_id(id);
    auto& current = active_runtime();
    if (!current.session.provider()) {
        setup_provider(current);
    }
    refresh_active_view();
    update_active_ui();
    refresh_session_picker();
    tui_->chat_view().show_system_message("Switched to " + current.session.title());
}

void Controller::show_sessions()
{
    std::ostringstream out;
    out << "Sessions (active marked with *):\n";
    size_t index = 1;
    for (const auto& data : session_store_.load_all()) {
        const auto* current = runtime(data.id);
        out << (data.id == active_session_id_ ? "* " : "  ")
            << index++ << ". " << data.id << "  "
            << (current ? current->session.title() :
                (data.title.empty() ? "Untitled" : data.title))
            << "  [" << data.history.size() << " messages]"
            << (current && current->processing.load() ? "  [running]" : "")
            << "\n";
    }
    out << "\nUsage: /sessions <id> to switch, /new to create, /sessions refresh to refresh this list.";
    tui_->chat_view().show_system_message(out.str());
}

void Controller::setup_tools()
{
    tool_registry_.register_tool(std::make_unique<ReadTool>());
    tool_registry_.register_tool(std::make_unique<WriteTool>());
    tool_registry_.register_tool(std::make_unique<EditTool>());
    tool_registry_.register_tool(std::make_unique<BashTool>());
    tool_registry_.register_tool(std::make_unique<SubAgentTool>(
        [this]() { return config_; },
        [this]() { return active_runtime().session.model(); }
    ));
    tool_registry_.register_tool(std::make_unique<SearchTool>(config_.exa));
    tool_registry_.register_tool(std::make_unique<GlobTool>());
    tool_registry_.register_tool(std::make_unique<GrepTool>());
}

void Controller::setup_commands()
{
    CommandInitializer::register_all(command_registry_);
}

void Controller::setup_provider(SessionRuntime& runtime, bool load_model_catalog)
{
    auto provider_config = config_.openrouter;
    provider_config.default_model = runtime.session.model();
    auto provider = std::make_unique<OpenRouterProvider>(provider_config);
    if (load_model_catalog) {
        model_catalog_ = provider->available_models();
    }
    runtime.session.set_provider(std::move(provider));
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
    setup_provider(active_runtime(), true);
    build_tools_json();

    auto project_ctx = ProjectContext::discover();

    tui_ = std::make_unique<TuiApp>();

    tui_->sidebar().set_project_context(project_ctx);
    tui_->sidebar().set_model(active_runtime().session.model());
    tui_->sidebar().set_token_count(
        active_runtime().session.total_usage().prompt_tokens,
        active_runtime().session.total_usage().completion_tokens);
    tui_->sidebar().set_context(active_runtime().session.context_usage(),
        active_runtime().session.provider()->context_window());
    tui_->chat_view().set_model(active_runtime().session.model());
    refresh_active_view();

    tui_->status_bar().set_model(active_runtime().session.model());
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
        handle_async_failure(active_session_id_, error);
    });

    if (!initial_prompt.empty()) {
        handle_user_input(initial_prompt);
    }

    tui_->run();
}

void Controller::handle_user_input(const std::string& text)
{
    if (text.empty()) return;

    // Session switching remains available while another session is streaming.
    if (active_runtime().processing.load() && text[0] != '/') return;

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
    if (cmd == "new") {
        if (!active_runtime().session.history().empty()) {
            create_session();
            setup_provider(active_runtime());
            refresh_active_view();
            update_active_ui();
            refresh_session_picker();
            tui_->chat_view().show_system_message("Created a new session.");
        } else {
            tui_->chat_view().show_system_message("The current session is already empty.");
        }
        tui_->request_refresh();
        return;
    }

    if (cmd == "sessions") {
        const std::string trimmed = args;
        if (trimmed.empty() || trimmed == "list" || trimmed == "refresh") {
            show_sessions();
        } else if (trimmed == "next" || trimmed == "prev") {
            const auto sessions = session_store_.load_all();
            if (sessions.size() > 1) {
                auto it = std::find_if(sessions.begin(), sessions.end(), [this](const auto& data) {
                    return data.id == active_session_id_;
                });
                const auto current_index = it == sessions.end()
                    ? 0
                    : static_cast<size_t>(std::distance(sessions.begin(), it));
                const auto offset = trimmed == "next" ? 1 : sessions.size() - 1;
                switch_session(sessions[(current_index + offset) % sessions.size()].id);
            } else {
                tui_->chat_view().show_system_message("There is only one session.");
            }
        } else if (!trimmed.empty() &&
                   std::all_of(trimmed.begin(), trimmed.end(), [](unsigned char ch) { return std::isdigit(ch); })) {
            const auto sessions = session_store_.load_all();
            const auto index = static_cast<size_t>(std::stoull(trimmed));
            if (index > 0 && index <= sessions.size()) {
                switch_session(sessions[index - 1].id);
            } else {
                tui_->chat_view().show_system_message("Session number is out of range.");
            }
        } else {
            switch_session(trimmed);
        }
        tui_->request_refresh();
        return;
    }

    if (active_runtime().processing.load()) return;
    auto* command = command_registry_.find(cmd);
    if (command) {
        auto& current = active_runtime();
        const std::string previous_model = current.session.model();
        CommandContext ctx{
            current.session,
            tui_->chat_view(),
            &command_registry_,
            &tool_registry_,
            &skill_registry_,
            [this]() { tui_->request_refresh(); },
            [this](const std::string& key) { set_api_key(key); },
            [this](const std::string& key) { set_exa_api_key(key); }
        };
        command->execute(args, ctx);
        if (current.session.model() != previous_model) {
            config_.openrouter.default_model = current.session.model();
            config_.save();
        }
        persist(current);
        tui_->status_bar().set_model(current.session.model());
        tui_->sidebar().set_model(current.session.model());
        tui_->chat_view().set_model(current.session.model());
        tui_->sidebar().set_context(
            current.session.context_usage(),
            current.session.provider() ? current.session.provider()->context_window() : 0
        );
        tui_->request_refresh();
    } else {
        tui_->chat_view().show_system_message("Unknown command: /" + cmd + ". Type /help for available commands.");
        tui_->request_refresh();
    }
}

void Controller::handle_normal_message(const std::string& text)
{
    auto& current = active_runtime();
    current.processing.store(true);
    current.abort_pending.store(false);
    current.tool_cancel_requested.store(false);

    Message user_msg;
    user_msg.role = MessageRole::User;
    user_msg.content = text;
    current.session.add_message(user_msg);
    persist(current);
    tui_->chat_view().add_message(user_msg);

    // Clear stream accumulators
    current.stream_content.clear();
    current.stream_tool_calls.clear();
    current.stream_finish_reason = FinishReason::Unknown;

    tui_->status_bar().set_status("Thinking...");
    tui_->set_typing_state(true);

    send_to_provider(active_session_id_);
}

void Controller::send_to_provider(const std::string& session_id)
{
    auto* current = runtime(session_id);
    if (!current) return;
    auto* provider = current->session.provider();
    if (!provider) {
        if (session_id == active_session_id_) {
            tui_->chat_view().show_system_message("Error: No provider configured.");
        }
        current->processing.store(false);
        return;
    }

    auto* tui = tui_.get();

    try {
        provider->send(
            current->session.history(),
            tools_json_,
            current->session.model(),
            [tui, this, session_id](Delta delta) {
                tui->post([this, session_id, delta = std::move(delta)]() { on_delta(session_id, delta); });
            },
            [tui, this, session_id](std::string error) {
                tui->post([this, session_id, error = std::move(error)]() { on_stream_error(session_id, error); });
            },
            [tui, this, session_id](Usage usage) {
                tui->post([this, session_id, usage]() { on_stream_done(session_id, usage); });
            }
        );
    } catch (const std::exception& e) {
        on_stream_error(session_id, std::string("Failed to start request: ") + e.what());
    } catch (...) {
        on_stream_error(session_id, "Failed to start request");
    }
}

void Controller::on_delta(const std::string& session_id, const Delta& delta)
{
    auto* current = runtime(session_id);
    if (!current || !current->processing.load() || current->tool_cancel_requested.load()) return;

    if (delta.content) {
        current->stream_content += *delta.content;
    }
    if (delta.tool_call) {
        int idx = delta.tool_call->index;
        auto& acc = current->stream_tool_calls[idx];
        if (!delta.tool_call->id.empty()) acc.id = delta.tool_call->id;
        if (!delta.tool_call->type.empty()) acc.type = delta.tool_call->type;
        if (!delta.tool_call->function_name.empty()) acc.function_name = delta.tool_call->function_name;
        acc.arguments = delta.tool_call->arguments;
        acc.index = idx;
    }
    if (delta.finish_reason) {
        current->stream_finish_reason = *delta.finish_reason;
    }
    if (session_id == active_session_id_) {
        tui_->chat_view().add_delta(delta);
    }
    persist(*current);
    tui_->request_refresh();
}

void Controller::on_stream_error(const std::string& session_id, const std::string& error)
{
    auto* current = runtime(session_id);
    if (!current) return;
    const bool was_aborted = current->abort_pending.load() || current->tool_cancel_requested.load();
    if (session_id == active_session_id_) {
        if (was_aborted) {
            tui_->chat_view().show_system_message("Response aborted.");
        } else {
            tui_->chat_view().show_system_message("Error: " + error);
        }
        tui_->status_bar().set_status(was_aborted ? "Aborted" : "Error");
        tui_->set_typing_state(false);
    }
    current->processing.store(false);
    current->abort_pending.store(false);
    current->tool_cancel_requested.store(false);
    persist(*current);
    tui_->request_refresh();
}

void Controller::on_stream_done(const std::string& session_id, Usage usage)
{
    auto* current = runtime(session_id);
    if (!current || !current->processing.load() || current->tool_cancel_requested.load()) return;

    current->session.add_usage(usage);
    current->session.set_context_usage(
        usage.prompt_tokens > 0
            ? usage.prompt_tokens
            : current->session.estimate_context_usage(*current->session.provider())
    );
    if (session_id == active_session_id_) {
        tui_->status_bar().set_token_count(
            current->session.total_usage().prompt_tokens,
            current->session.total_usage().completion_tokens);
        tui_->sidebar().set_token_count(
            current->session.total_usage().prompt_tokens,
            current->session.total_usage().completion_tokens);
        tui_->sidebar().set_context(current->session.context_usage(),
            current->session.provider()->context_window());
    }

    // Build the final assistant message from accumulated data
    Message assistant_msg;
    assistant_msg.role = MessageRole::Assistant;
    assistant_msg.content = current->stream_content;

    // Extract tool calls into a local before any move operations
    auto tool_calls_to_process = std::move(current->stream_tool_calls);
    current->stream_tool_calls.clear();
    for (const auto& [idx, tc] : tool_calls_to_process) {
        assistant_msg.tool_calls.push_back(tc);
    }
    current->session.add_message(assistant_msg);
    persist(*current);

    // Check if we need to execute tool calls
    if (current->stream_finish_reason == FinishReason::ToolCalls && !tool_calls_to_process.empty()) {
        execute_tool_calls_and_continue(session_id, std::move(tool_calls_to_process));
    } else {
        current->processing.store(false);
        if (session_id == active_session_id_) {
            tui_->status_bar().set_status("Ready");
            tui_->set_typing_state(false);
        }
    }
}

void Controller::execute_tool_calls_and_continue(const std::string& session_id, std::map<int, ToolCall> tool_calls)
{
    // Keep filesystem, regex, and process tools off the UI event loop. This
    // also prevents a tool callback from re-entering the provider lifecycle.
    auto* current = runtime(session_id);
    if (!current) return;
    if (session_id == active_session_id_) {
        tui_->status_bar().set_status("Running tools...");
        tui_->set_typing_state(true);
    }
    current->tool_execution_active.store(true);

    try {
        std::lock_guard<std::mutex> lock(current->tool_worker_mutex);
        if (current->tool_worker.joinable()) {
            if (current->tool_worker.get_id() == std::this_thread::get_id()) {
                throw std::runtime_error("Tool worker attempted to replace itself");
            }
            current->tool_worker.join();
        }

        current->tool_worker = std::thread([this, session_id, tool_calls = std::move(tool_calls)]() mutable noexcept {
            try {
                std::vector<ToolExecutionResult> results;
                results.reserve(tool_calls.size());

                for (auto& [idx, tc] : tool_calls) {
                    (void)idx;
                    ToolExecutionResult execution;
                    execution.call = tc;

                    auto* runtime = this->runtime(session_id);
                    if (!runtime) return;
                    if (runtime->tool_cancel_requested.load()) {
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
                                    std::to_string(runtime->tool_display_sequence.fetch_add(1));
                                int timeout = args.value("timeout", kDefaultBashTimeoutMs);
                                if (timeout <= 0) timeout = kDefaultBashTimeoutMs;
                                const bool explicit_timeout = args.contains("timeout") &&
                                    args["timeout"].is_number_integer() &&
                                    args["timeout"].get<int>() > 0;
                                const std::string timeout_label = std::to_string(timeout) +
                                    "ms" + (explicit_timeout ? "" : " (default)");
                                const std::string key = execution.display_key;
                                const std::string command = args.value("command", "");
                                tui_->post([this, session_id, key, command, timeout_label]() {
                                    if (session_id == active_session_id_)
                                        tui_->chat_view().show_bash_started(key, command, timeout_label);
                                });
                                on_output = [this, session_id, key](const std::string& output) {
                                    if (output.empty()) return;
                                    tui_->post([this, session_id, key, output]() {
                                        if (session_id == active_session_id_)
                                            tui_->chat_view().append_bash_output(key, output);
                                    });
                                };
                            } else if (tc.function_name == "search") {
                                execution.display_key = "search-" +
                                    std::to_string(runtime->tool_display_sequence.fetch_add(1));
                                const std::string key = execution.display_key;
                                const std::string query = args.value("query", "");
                                tui_->post([this, session_id, key, query]() {
                                    if (session_id == active_session_id_)
                                        tui_->chat_view().show_bash_started(
                                            key, query, "Exa", "search");
                                });
                            } else if (tc.function_name == "sub_agent") {
                                execution.display_key = "sub-agent-" +
                                    std::to_string(runtime->tool_display_sequence.fetch_add(1));
                                const std::string key = execution.display_key;
                                const std::string task = args.value("task", "");
                                const std::string mode = args.value("mode", "R");
                                tui_->post([this, session_id, key, task, mode]() {
                                    if (session_id == active_session_id_)
                                        tui_->chat_view().show_subagent_started(key, task, mode);
                                });
                                on_output = [this, session_id, key](const std::string& event) {
                                    tui_->post([this, session_id, key, event]() {
                                        if (session_id == active_session_id_)
                                            tui_->chat_view().update_subagent(key, event);
                                    });
                                };
                            }

                            ToolCancelCallback should_cancel = [runtime]() {
                                return runtime->tool_cancel_requested.load();
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

                    tui_->post([this, session_id, results = std::move(results)]() mutable {
                    finish_tool_execution(session_id, std::move(results));
                });
            } catch (const std::exception& e) {
                try {
                    std::string error = std::string("Tool worker failed: ") + e.what();
                        tui_->post([this, session_id, error = std::move(error)]() {
                        handle_async_failure(session_id, error);
                    });
                } catch (...) {
                }
            } catch (...) {
                try {
                        tui_->post([this, session_id]() {
                        handle_async_failure(session_id, "Tool worker failed with an unknown error");
                    });
                } catch (...) {
                }
            }
        });
    } catch (const std::exception& e) {
        current->tool_execution_active.store(false);
        if (session_id == active_session_id_) {
            tui_->chat_view().show_system_message("Tool execution failed: " + std::string(e.what()));
            tui_->status_bar().set_status("Error");
            tui_->set_typing_state(false);
        }
        current->processing.store(false);
    }
}

void Controller::handle_async_failure(const std::string& session_id, const std::string& error) noexcept
{
    auto* current = runtime(session_id);
    if (!current) return;
    current->processing.store(false);
    current->abort_pending.store(false);
    current->tool_cancel_requested.store(false);
    current->tool_execution_active.store(false);
    try {
        if (auto* provider = current->session.provider()) {
            provider->abort();
        }
        if (session_id == active_session_id_) {
            tui_->set_typing_state(false);
            tui_->status_bar().set_status("Error");
            tui_->chat_view().show_system_message("Internal error: " + error);
        }
        persist(*current);
        tui_->request_refresh();
    } catch (...) {
        // Keep the TUI alive even if rendering the diagnostic itself fails.
    }
}

void Controller::finish_tool_execution(const std::string& session_id, std::vector<ToolExecutionResult> results)
{
    auto* current = runtime(session_id);
    if (!current) return;
    const bool cancelled = current->tool_cancel_requested.load();
    current->tool_execution_active.store(false);

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
        current->session.add_message(result_msg);

        if (session_id == active_session_id_ &&
            (tc.function_name == "read" || tc.function_name == "glob" || tc.function_name == "grep")) {
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
        } else if (session_id == active_session_id_ &&
                   (tc.function_name == "bash" || tc.function_name == "search")) {
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
        } else if (session_id == active_session_id_ && tc.function_name == "sub_agent") {
            if (!execution.display_key.empty()) {
                tui_->chat_view().finish_subagent(
                    execution.display_key, execution.output, execution.success);
            } else {
                tui_->chat_view().show_system_message(
                    "sub_agent : " + execution.output);
            }
        } else if (session_id == active_session_id_ &&
                   (tc.function_name == "write" || tc.function_name == "edit") &&
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

    current->stream_content.clear();
    current->stream_tool_calls.clear();
    current->stream_finish_reason = FinishReason::Unknown;
    persist(*current);

    if (cancelled) {
        current->processing.store(false);
        current->abort_pending.store(false);
        current->tool_cancel_requested.store(false);
        if (session_id == active_session_id_) {
            tui_->status_bar().set_status("Aborted");
            tui_->set_typing_state(false);
        }
        tui_->request_refresh();
        return;
    }

    if (session_id == active_session_id_) {
        tui_->status_bar().set_status("Thinking...");
        tui_->set_typing_state(true);
    }
    send_to_provider(session_id);
}

void Controller::handle_escape_key()
{
    auto& current = active_runtime();
    if (!current.processing.load()) {
        tui_->stop();
        return;
    }

    if (current.tool_cancel_requested.load()) {
        return;
    }

    if (current.abort_pending.load()) {
        // Second press: actually abort
        current.tool_cancel_requested.store(true);
        auto* provider = current.session.provider();
        if (provider) {
            provider->abort();
        }
        tui_->status_bar().set_status("Aborting...");
        tui_->request_refresh();
        current.abort_pending.store(false);
        // Keep processing_ true until the provider/tool worker posts its
        // completion. This prevents a new request from racing stale callbacks.
        if (!current.tool_execution_active.load()) {
            tui_->set_typing_state(true);
        }
    } else {
        // First press: show pending
        current.abort_pending.store(true);
        last_abort_press_ = std::chrono::steady_clock::now();
        tui_->status_bar().set_status("Press Esc again to abort");
        tui_->request_refresh();
    }
}

void Controller::reset_abort_pending()
{
    auto& current = active_runtime();
    if (current.abort_pending.load()) {
        current.abort_pending.store(false);
        current.tool_cancel_requested.store(false);
        if (current.processing.load()) {
            tui_->status_bar().set_status("Thinking...");
            tui_->request_refresh();
        }
    }
}

void Controller::set_api_key(const std::string& api_key)
{
    config_.openrouter.api_key = api_key;
    config_.save();

    // Refresh every provider that is idle. A running session keeps its
    // in-flight request and will use the new key the next time it starts.
    for (auto& [id, runtime] : sessions_) {
        (void)id;
        if (!runtime->processing.load() && runtime->session.provider()) {
            runtime->session.provider()->set_api_key(api_key);
        }
    }

    auto* provider = active_runtime().session.provider();
    if (!provider) return;

    provider->set_model(active_runtime().session.model());
    model_catalog_ = provider->available_models();
    tui_->input_bar().set_models(model_catalog_);
    refresh_session_picker();
    tui_->sidebar().set_context(active_runtime().session.context_usage(), provider->context_window());
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
