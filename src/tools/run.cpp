#include "tools/run.h"
#include "process/runner.h"

std::string RunTool::name() const { return "run"; }

std::string RunTool::description() const
{
    return "Execute a shell command in the project directory. Returns stdout, stderr, and exit code.";
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
                {"description", "Timeout in milliseconds"},
                {"default", 30000}
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
    std::string command = params["command"].get<std::string>();
    int timeout = params.value("timeout", 30000);
    std::string workdir = params.value("workdir", "");

    ProcessResult result = ProcessRunner::run(command, workdir, timeout);

    std::string output;
    if (!result.stdout_str.empty()) {
        output += result.stdout_str;
    }
    if (!result.stderr_str.empty()) {
        if (!output.empty()) output += "\n";
        output += "stderr:\n" + result.stderr_str;
    }

    output += "\nExit code: " + std::to_string(result.exit_code);

    if (result.timed_out) {
        return ToolResult::fail("Command timed out after " + std::to_string(timeout) + "ms\n" + output);
    }

    if (result.exit_code == 0) {
        return ToolResult::ok(output);
    }

    return ToolResult::fail(output);
}
