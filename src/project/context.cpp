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

        std::string branch = "unknown";
        fs::path head_path = fs::path(ctx.root_path) / ".git" / "HEAD";
        if (fs::exists(head_path)) {
            std::ifstream ifs(head_path);
            std::string line;
            if (std::getline(ifs, line)) {
                if (line.rfind("ref: refs/heads/", 0) == 0) {
                    branch = line.substr(16);
                    while (!branch.empty() && (branch.back() == '\r' || branch.back() == '\n' || branch.back() == ' ')) {
                        branch.pop_back();
                    }
                } else {
                    if (line.size() > 7) {
                        branch = line.substr(0, 7);
                    } else {
                        branch = line;
                    }
                }
            }
        }
        ctx.git_branch = branch;
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
