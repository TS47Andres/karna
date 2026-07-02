#include "project/context.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

std::optional<std::string> ProjectContext::find_git_root(const std::string& path)
{
    fs::path current = fs::absolute(path);
    while (true) {
        if (fs::exists(current / ".git")) {
            return current.string();
        }
        if (!current.has_parent_path()) break;
        current = current.parent_path();
    }
    return std::nullopt;
}

ProjectContext ProjectContext::discover(const std::string& start_path)
{
    ProjectContext ctx;
    ctx.root_path = fs::absolute(start_path).string();

    auto git_root = find_git_root(start_path);
    if (git_root) {
        ctx.has_git = true;
        ctx.root_path = *git_root;
    }

    return ctx;
}

std::string ProjectContext::summarize() const
{
    std::ostringstream out;
    out << "Project: " << root_path;
    if (has_git) {
        out << " (git)";
    }
    return out.str();
}
