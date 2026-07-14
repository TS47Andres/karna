#include "tools/run.h"
#include "process/runner.h"

std::string RunTool::name() const { return "bash"; }

std::string RunTool::description() const
{
    return "Execute a bash shell command in the project directory and stream its output.";
}

json RunTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"command", {
                {"type", "string"},
                {"description", "Shell command to execute"}
            }},
            {"timeout", {
                {"type", "integer"},
                {"description", "Optional timeout in milliseconds"}
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

ToolResult RunTool::execute(const json& params)
{
    return execute_stream(params, {});
}

ToolResult RunTool::execute_stream(const json& params, ToolOutputCallback on_output)
{
    std::string command = params["command"].get<std::string>();
    int timeout = params.value("timeout", -1);
    std::string workdir = params.value("workdir", "");

    ProcessResult result = ProcessRunner::run(command, workdir, timeout, std::move(on_output));

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
    };

    if (result.timed_out) {
        return {false, "Command timed out after " + std::to_string(timeout) + "ms\n" + output, std::move(data)};
    }

    if (result.exit_code == 0) {
        return ToolResult::ok(output, std::move(data));
    }

    return {false, output, std::move(data)};
}
