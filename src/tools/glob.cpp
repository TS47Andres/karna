#include "tools/glob.h"

#include <filesystem>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

static void recursive_glob(const fs::path& base, const std::string& pattern, std::vector<std::string>& results, int max_depth, int depth = 0)
{
    if (max_depth >= 0 && depth > max_depth) return;

    try {
        for (const auto& entry : fs::directory_iterator(base)) {
            std::string entry_name = entry.path().filename().string();
            bool match = false;

            if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos) {
                size_t star = pattern.find('*');
                size_t qmark = pattern.find('?');
                size_t ext_dot = pattern.rfind('.');

                if (star == 0 && ext_dot != std::string::npos) {
                    std::string ext = pattern.substr(ext_dot);
                    if (entry_name.size() >= ext.size() &&
                        entry_name.compare(entry_name.size() - ext.size(), ext.size(), ext) == 0) {
                        match = true;
                    }
                } else if (star == std::string::npos && qmark != std::string::npos) {
                    if (entry_name.size() == pattern.size()) {
                        match = true;
                        for (size_t i = 0; i < pattern.size(); ++i) {
                            if (pattern[i] != '?' && pattern[i] != entry_name[i]) {
                                match = false;
                                break;
                            }
                        }
                    }
                }
            } else {
                match = (entry_name == pattern);
            }

            if (match) {
                results.push_back(entry.path().string());
            }

            if (entry.is_directory()) {
                recursive_glob(entry.path(), pattern, results, max_depth, depth + 1);
            }
        }
    } catch (...) {
    }
}

std::string GlobTool::name() const { return "glob"; }

std::string GlobTool::description() const
{
    return "Find files matching a glob pattern. Returns paths of all matching files recursively.";
}

json GlobTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {
                {"type", "string"},
                {"description", "Glob pattern to match (e.g., \"**/*.cpp\", \"src/**/*.h\", \"*.py\")"}
            }},
            {"path", {
                {"type", "string"},
                {"description", "Root directory to search in"},
                {"default", "."}
            }},
            {"max_depth", {
                {"type", "integer"},
                {"description", "Maximum directory depth to search (-1 for unlimited)"},
                {"default", -1}
            }}
        }},
        {"required", {"pattern"}}
    };
}

ToolResult GlobTool::execute(const json& params)
{
    std::string pattern = params["pattern"].get<std::string>();
    std::string root = params.value("path", ".");
    int max_depth = params.value("max_depth", -1);

    if (!fs::exists(root)) {
        return ToolResult::fail("Directory not found: " + root);
    }

    std::vector<std::string> results;
    recursive_glob(fs::path(root), pattern, results, max_depth);
    std::sort(results.begin(), results.end());

    if (results.empty()) {
        return ToolResult::ok("No files matching: " + pattern);
    }

    std::string output;
    for (const auto& r : results) {
        output += r + "\n";
    }

    return ToolResult::ok(output);
}
