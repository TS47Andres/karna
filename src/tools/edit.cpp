#include "tools/edit.h"
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

std::string EditTool::name() const { return "edit"; }

std::string EditTool::description() const
{
    return "Apply a search-and-replace edit to an existing file. Finds the exact old_string and replaces it with new_string.";
}

json EditTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to edit"}
            }},
            {"old_string", {
                {"type", "string"},
                {"description", "Exact text to search for in the file. Must match exactly including whitespace."}
            }},
            {"new_string", {
                {"type", "string"},
                {"description", "Text to replace old_string with"}
            }}
        }},
        {"required", {"path", "old_string", "new_string"}}
    };
}

ToolResult EditTool::execute(const json& params)
{
    std::string path = params["path"].get<std::string>();
    std::string old_str = params["old_string"].get<std::string>();
    std::string new_str = params["new_string"].get<std::string>();

    if (!fs::exists(path)) {
        return ToolResult::fail("File not found: " + path);
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        return ToolResult::fail("Failed to open file: " + path);
    }

    std::stringstream buf;
    buf << file.rdbuf();
    std::string content = buf.str();
    file.close();

    size_t pos = content.find(old_str);
    if (pos == std::string::npos) {
        return ToolResult::fail("Could not find old_string in file. The text must match exactly.");
    }

    content.replace(pos, old_str.length(), new_str);

    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) {
        return ToolResult::fail("Failed to write file: " + path);
    }
    out << content;
    out.close();

    return ToolResult::ok("Successfully applied edit to " + fs::absolute(path).string());
}
