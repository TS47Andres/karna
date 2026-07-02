#include "tools/grep.h"

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace fs = std::filesystem;

std::string GrepTool::name() const { return "grep"; }

std::string GrepTool::description() const
{
    return "Search for a regex pattern in file contents. Returns matching file paths and line numbers.";
}

json GrepTool::parameters() const
{
    return {
        {"type", "object"},
        {"properties", {
            {"pattern", {
                {"type", "string"},
                {"description", "Regular expression pattern to search for"}
            }},
            {"include", {
                {"type", "string"},
                {"description", "File pattern to include (e.g. \"*.cpp\", \"*.{ts,tsx}\")"},
                {"default", ""}
            }},
            {"path", {
                {"type", "string"},
                {"description", "Root directory to search in"},
                {"default", "."}
            }},
            {"max_results", {
                {"type", "integer"},
                {"description", "Maximum number of matches to return"},
                {"default", 50}
            }}
        }},
        {"required", {"pattern"}}
    };
}

static bool matches_include(const std::string& filename, const std::string& include_pattern)
{
    if (include_pattern.empty()) return true;

    std::string ext = fs::path(filename).extension().string();
    if (include_pattern.find('*') != std::string::npos) {
        std::string pattern_ext = include_pattern.substr(include_pattern.find('*') + 1);
        if (ext == pattern_ext) return true;
        if (include_pattern.find(ext) != std::string::npos) return true;
        return false;
    }
    return filename.find(include_pattern) != std::string::npos;
}

ToolResult GrepTool::execute(const json& params)
{
    std::string pattern_str = params["pattern"].get<std::string>();
    std::string include = params.value("include", "");
    std::string root = params.value("path", ".");
    int max_results = params.value("max_results", 50);

    if (!fs::exists(root)) {
        return ToolResult::fail("Directory not found: " + root);
    }

    try {
        std::regex pattern(pattern_str, std::regex::ECMAScript | std::regex::icase);
        std::ostringstream out;
        int matches = 0;

        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            if (!matches_include(entry.path().filename().string(), include)) continue;

            std::ifstream file(entry.path());
            std::string line;
            int line_num = 0;

            while (std::getline(file, line)) {
                ++line_num;
                if (std::regex_search(line, pattern)) {
                    out << entry.path().string() << ":" << line_num << ": " << line << "\n";
                    ++matches;
                    if (matches >= max_results) {
                        out << "... [max results reached, truncated]\n";
                        return ToolResult::ok(out.str());
                    }
                }
            }
        }

        if (matches == 0) {
            return ToolResult::ok("No matches found for: " + pattern_str);
        }

        return ToolResult::ok(out.str());
    } catch (const std::regex_error& e) {
        return ToolResult::fail("Invalid regex pattern: " + std::string(e.what()));
    }
}
