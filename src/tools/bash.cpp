#include "tools/bash.h"
#include "process/runner.h"

namespace {
constexpr int kDefaultTimeoutMs = 60 * 1000;
}

std::string BashTool::name() const { return "bash"; }

std::string BashTool::description() const
{
    return "Execute a shell command in the project directory and stream its output. On Windows, use native PowerShell syntax.";
}

json BashTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"command", {
                {"type", "string"},
                {"description", "One complete shell command string. On Windows, use Windows PowerShell syntax: semicolons for sequential commands, PowerShell pipelines, Set-Location, and $env:NAME; do not use CMD or Bash-only syntax."}
            }},
            {"timeout", {
                {"type", "integer"},
                {"description", "Optional timeout in milliseconds (default: 60000)"},
                {"minimum", 1},
                {"default", kDefaultTimeoutMs}
            }},
            {"workdir", {
                {"type", "string"},
                {"description", "Working directory for the command"},
                {"default", ""}
            }}
        }},
        {"required", {"command"}}
    };
}

ToolResult BashTool::execute(const json& params)
{
    return execute_stream(params, {}, {});
}

ToolResult BashTool::execute_stream(const json& params, ToolOutputCallback on_output,
                                    ToolCancelCallback should_cancel)
{
    std::string command = params["command"].get<std::string>();
    int timeout = params.value("timeout", kDefaultTimeoutMs);
    if (timeout <= 0) {
        timeout = kDefaultTimeoutMs;
    }
    std::string workdir = params.value("workdir", "");

    ProcessResult result = ProcessRunner::run(
        command, workdir, timeout, std::move(on_output), std::move(should_cancel));

    std::string output;
    if (!result.stdout_str.empty()) {
        output += result.stdout_str;
    }
    if (!result.stderr_str.empty()) {
        if (!output.empty()) output += "\n";
        output += "stderr:\n" + result.stderr_str;
    }

    output += "\nExit code: " + std::to_string(result.exit_code);

    json data = {
        {"command", command},
        {"timeout", timeout},
        {"timeout_set", params.contains("timeout")},
        {"exit_code", result.exit_code},
        {"timed_out", result.timed_out},
        {"cancelled", result.cancelled},
    };

    if (result.timed_out) {
        return {false, "Command timed out after " + std::to_string(timeout) + "ms\n" + output, std::move(data)};
    }

    if (result.cancelled) {
        return {false, "Command cancelled by user\n" + output, std::move(data)};
    }

    if (result.exit_code == 0) {
        return ToolResult::ok(output, std::move(data));
    }

    return {false, output, std::move(data)};
}
