#include "tools/read.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

std::string ReadTool::name() const { return "read"; }

std::string ReadTool::description() const
{
    return "Read the contents of a file. Returns the full file content or an error if the file does not exist.";
}

json ReadTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to read"}
            }},
            {"offset", {
                {"type", "integer"},
                {"description", "Line number to start reading from (1-indexed)"},
                {"default", 1}
            }},
            {"limit", {
                {"type", "integer"},
                {"description", "Maximum number of lines to read"},
                {"default", 2000}
            }}
        }},
        {"required", {"path"}}
    };
}

ToolResult ReadTool::execute(const json& params)
{
    std::string path = params["path"].get<std::string>();
    int offset = params.value("offset", 1);
    int limit = params.value("limit", 2000);

    if (!fs::exists(path)) {
        return ToolResult::fail("File not found: " + path);
    }
    if (!fs::is_regular_file(path)) {
        return ToolResult::fail("Not a regular file: " + path);
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return ToolResult::fail("Failed to open file: " + path);
    }

    std::ostringstream result;
    std::string line;
    int line_num = 0;
    int printed = 0;

    while (std::getline(file, line)) {
        ++line_num;
        if (line_num < offset) continue;
        if (printed >= limit) break;
        result << line_num << ": " << line << "\n";
        ++printed;
    }

    if (printed == 0) {
        return ToolResult::ok("(empty range)");
    }

    return ToolResult::ok(result.str());
}
