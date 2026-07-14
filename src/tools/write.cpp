#include "tools/write.h"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

std::string WriteTool::name() const { return "write"; }

std::string WriteTool::description() const
{
    return "Write content to a file. Creates the file if it does not exist. Overwrites existing content.";
}

json WriteTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"path", {
                {"type", "string"},
                {"description", "Path to the file to write"}
            }},
            {"content", {
                {"type", "string"},
                {"description", "Content to write to the file"}
            }}
        }},
        {"required", {"path", "content"}}
    };
}

ToolResult WriteTool::execute(const json& params)
{
    std::string path = params["path"].get<std::string>();
    std::string content = params["content"].get<std::string>();

    std::string previous_content;
    if (fs::exists(path) && fs::is_regular_file(path)) {
        std::ifstream existing(path, std::ios::binary);
        previous_content.assign(
            std::istreambuf_iterator<char>(existing),
            std::istreambuf_iterator<char>());
    }

    fs::path parent = fs::path(path).parent_path();
    if (!parent.empty() && !fs::exists(parent)) {
        fs::create_directories(parent);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return ToolResult::fail("Failed to open file for writing: " + path);
    }

    file << content;
    file.close();

    return ToolResult::ok(
        "Successfully wrote " + std::to_string(content.size()) + " bytes to " + fs::absolute(path).string(),
        {
            {"path", path},
            {"before", previous_content},
            {"after", content},
        });
}
