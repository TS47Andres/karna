#include "tools/sub_agent.h"

#include "providers/openrouter.h"
#include "tools/bash.h"
#include "tools/edit.h"
#include "tools/glob.h"
#include "tools/grep.h"
#include "tools/read.h"
#include "tools/registry.h"
#include "tools/search.h"
#include "tools/write.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

namespace {

struct ModelResponse {
    std::string content;
    std::map<int, ToolCall> tool_calls;
    Usage usage;
    std::string error;
    bool cancelled{false};
};

constexpr const char* kReadOnlyInstructions = R"(
You are a read-only sub-agent spawned by Karna. Investigate the assigned task
carefully using the available inspection and search tools. You must not modify
files, execute shell commands, or make external changes. Work independently and
return a detailed report to the main agent when finished.
)";

constexpr const char* kReadWriteInstructions = R"(
You are a read/write sub-agent spawned by Karna. Work independently on the
assigned task using the available tools. You may modify files when necessary,
but stay within the project directory and verify meaningful changes. Return a
detailed report to the main agent when finished.
)";

constexpr const char* kReportInstructions = R"(
Do not ask the user questions. When the task is complete, respond with a
detailed report containing: Summary, Findings or Changes, Files, Verification,
and Remaining Issues. Use HTML tables (<table>, <thead>, <tbody>, <tr>, <th>,
and <td>) for tabular comparisons or repeated fields; do not use pipe tables.
If you could not complete something, state the exact blocker and what remains.
)";

void emit(const ToolOutputCallback& callback, const std::string& event)
{
    if (callback) {
        callback(event);
    }
}

void emit_payload(const ToolOutputCallback& callback, const std::string& kind,
                  const std::string& payload)
{
    emit(callback, kind + "\n" + payload);
}

json tool_definitions(const ToolRegistry& registry)
{
    json tools = json::array();
    for (const auto* tool : registry.all()) {
        tools.push_back({
            {"type", "function"},
            {"function", {
                {"name", tool->name()},
                {"description", tool->description()},
                {"parameters", tool->parameters()}
            }}
        });
    }
    return tools;
}

void register_subagent_tools(ToolRegistry& registry, const std::string& mode,
                             const ExaConfig& exa)
{
    registry.register_tool(std::make_unique<ReadTool>());
    registry.register_tool(std::make_unique<GlobTool>());
    registry.register_tool(std::make_unique<GrepTool>());
    registry.register_tool(std::make_unique<SearchTool>(exa));

    if (mode == "RW") {
        registry.register_tool(std::make_unique<WriteTool>());
        registry.register_tool(std::make_unique<EditTool>());
        registry.register_tool(std::make_unique<BashTool>());
    }
}

ModelResponse request_model(OpenRouterProvider& provider,
                            const std::vector<Message>& history,
                            const json& tools,
                            const std::string& model,
                            const ToolCancelCallback& should_cancel)
{
    ModelResponse response;
    std::mutex mutex;
    std::condition_variable condition;
    bool finished = false;

    try {
        provider.send(
            history,
            tools,
            model,
            [&response](Delta delta) {
                if (delta.content) {
                    response.content += *delta.content;
                }
                if (delta.tool_call) {
                    const auto& incoming = *delta.tool_call;
                    auto& call = response.tool_calls[incoming.index];
                    if (!incoming.id.empty()) call.id = incoming.id;
                    if (!incoming.type.empty()) call.type = incoming.type;
                    if (!incoming.function_name.empty()) call.function_name = incoming.function_name;
                    call.arguments = incoming.arguments;
                    call.index = incoming.index;
                }
            },
            [&response, &mutex, &condition, &finished](std::string error) {
                std::lock_guard<std::mutex> lock(mutex);
                response.error = std::move(error);
                finished = true;
                condition.notify_one();
            },
            [&response, &mutex, &condition, &finished](Usage usage) {
                std::lock_guard<std::mutex> lock(mutex);
                response.usage = usage;
                finished = true;
                condition.notify_one();
            }
        );

        std::unique_lock<std::mutex> lock(mutex);
        while (!finished) {
            if (should_cancel && should_cancel()) {
                response.cancelled = true;
                break;
            }
            condition.wait_for(lock, std::chrono::milliseconds(100));
        }
        lock.unlock();

        if (response.cancelled) {
            provider.abort();
        } else {
            // Join the provider worker before its callback-owned state goes
            // out of scope and before another sub-agent request starts.
            provider.abort();
        }
    } catch (const std::exception& error) {
        response.error = error.what();
        provider.abort();
    } catch (...) {
        response.error = "Unknown provider failure";
        provider.abort();
    }

    return response;
}

} // namespace

SubAgentTool::SubAgentTool(ConfigGetter config_getter, ModelGetter model_getter)
    : config_getter_(std::move(config_getter))
    , model_getter_(std::move(model_getter))
{}

std::string SubAgentTool::name() const
{
    return "sub_agent";
}

std::string SubAgentTool::description() const
{
    return "Spawn an independent sub-agent to investigate or complete a task. "
           "Use mode R for read-only investigation and RW when the sub-agent must modify files. "
           "The sub-agent returns a detailed report. Multiple independent sub-agent calls are supported.";
}

json SubAgentTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"task", {
                {"type", "string"},
                {"description", "A self-contained task for the sub-agent, including relevant files, constraints, and the desired result."}
            }},
            {"mode", {
                {"type", "string"},
                {"enum", {"R", "RW"}},
                {"default", "R"},
                {"description", "R investigates without changing files; RW may read, edit, write, and run PowerShell commands."}
            }}
        }},
        {"required", {"task"}}
    };
}

ToolResult SubAgentTool::execute(const json& params)
{
    return execute_stream(params, {}, {});
}

ToolResult SubAgentTool::execute_stream(const json& params, ToolOutputCallback on_output,
                                        ToolCancelCallback should_cancel)
{
    const std::string task = params.value("task", "");
    if (task.empty()) {
        return ToolResult::fail("Sub-agent task cannot be empty");
    }

    std::string mode = params.value("mode", "R");
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (mode != "R" && mode != "RW") {
        return ToolResult::fail("Invalid sub-agent mode. Use R or RW.");
    }

    if (!config_getter_ || !model_getter_) {
        return ToolResult::fail("Sub-agent configuration is unavailable");
    }

    const Config config = config_getter_();
    const std::string model = model_getter_();
    ProviderConfig provider_config = config.openrouter;
    provider_config.default_model = model;
    OpenRouterProvider provider(provider_config);

    ToolRegistry subagent_tools;
    register_subagent_tools(subagent_tools, mode, config.exa);
    const json tools = tool_definitions(subagent_tools);

    std::vector<Message> history;
    history.push_back({
        MessageRole::System,
        std::string(mode == "R" ? kReadOnlyInstructions : kReadWriteInstructions) +
            kReportInstructions,
        std::nullopt,
        {},
        std::nullopt
    });
    history.push_back({MessageRole::User, task});

    emit(on_output, "status:thinking");

    int tools_used = 0;
    std::string latest_tool;
    Usage total_usage;

    while (true) {
        if (should_cancel && should_cancel()) {
            return {false, "Sub-agent cancelled by user", {
                {"mode", mode},
                {"tools_used", tools_used},
                {"latest_tool", latest_tool},
                {"cancelled", true}
            }};
        }

        ModelResponse response = request_model(
            provider, history, tools, model, should_cancel);
        total_usage.prompt_tokens += response.usage.prompt_tokens;
        total_usage.completion_tokens += response.usage.completion_tokens;
        total_usage.total_tokens += response.usage.total_tokens;

        if (response.cancelled || (should_cancel && should_cancel())) {
            return {false, "Sub-agent cancelled by user", {
                {"mode", mode},
                {"tools_used", tools_used},
                {"latest_tool", latest_tool},
                {"cancelled", true}
            }};
        }
        if (!response.error.empty()) {
            return {false, "Sub-agent provider error: " + response.error, {
                {"mode", mode},
                {"tools_used", tools_used},
                {"latest_tool", latest_tool}
            }};
        }

        if (response.tool_calls.empty()) {
            const std::string report = response.content.empty()
                ? "Sub-agent completed without a final report."
                : response.content;
            return {true, report, {
                {"mode", mode},
                {"model", model},
                {"tools_used", tools_used},
                {"latest_tool", latest_tool},
                {"prompt_tokens", total_usage.prompt_tokens},
                {"completion_tokens", total_usage.completion_tokens}
            }};
        }

        if (!response.content.empty()) {
            emit_payload(on_output, "assistant", response.content);
        }

        Message assistant;
        assistant.role = MessageRole::Assistant;
        assistant.content = response.content;
        for (const auto& [index, call] : response.tool_calls) {
            (void)index;
            assistant.tool_calls.push_back(call);
        }
        history.push_back(std::move(assistant));

        for (const auto& [index, call] : response.tool_calls) {
            (void)index;
            if (should_cancel && should_cancel()) {
                return {false, "Sub-agent cancelled by user", {
                    {"mode", mode},
                    {"tools_used", tools_used},
                    {"latest_tool", latest_tool},
                    {"cancelled", true}
                }};
            }

            latest_tool = call.function_name.empty() ? "unknown" : call.function_name;
            ++tools_used;
            emit_payload(on_output, "tool_call", latest_tool + "\n" +
                (call.arguments.empty() ? "{}" : call.arguments));

            ToolResult result;
            try {
                Tool* tool = subagent_tools.find(call.function_name);
                if (!tool) {
                    result = ToolResult::fail("Tool is not available in " + mode + " mode");
                } else {
                    const json arguments = json::parse(call.arguments.empty() ? "{}" : call.arguments);
                    result = tool->execute_stream(
                        arguments,
                        [&on_output, &latest_tool](const std::string& output) {
                            if (!output.empty()) {
                                emit_payload(on_output, "tool_output",
                                    latest_tool + "\n" + output);
                            }
                        },
                        should_cancel);
                }
            } catch (const std::exception& error) {
                result = ToolResult::fail(std::string("Sub-agent tool failed: ") + error.what());
            } catch (...) {
                result = ToolResult::fail("Sub-agent tool failed with an unknown error");
            }

            emit_payload(on_output, "tool_result", latest_tool + "\n" +
                (result.success ? result.output : "Error: " + result.output));

            Message tool_message;
            tool_message.role = MessageRole::Tool;
            tool_message.name = call.function_name;
            tool_message.tool_call_id = call.id;
            tool_message.content = result.success ? result.output : "Error: " + result.output;
            history.push_back(std::move(tool_message));
        }
    }
}
