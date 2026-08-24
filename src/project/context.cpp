#include "project/context.h"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

std::optional<std::string> ProjectContext::find_git_root(const std::string& path)
{
    std::error_code error;
    fs::path current = fs::absolute(path, error);
    if (error) {
        return std::nullopt;
    }
    while (true) {
        if (fs::exists(current / ".git", error) && !error) {
            return current.string();
        }
        error.clear();
        if (!current.has_parent_path()) break;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return std::nullopt;
}

ProjectContext ProjectContext::discover(const std::string& start_path)
{
    ProjectContext ctx;
    std::error_code error;
    const auto absolute_path = fs::absolute(start_path, error);
    ctx.root_path = error ? start_path : absolute_path.string();

    auto git_root = find_git_root(start_path);
    if (git_root) {
        ctx.has_git = true;
        ctx.root_path = *git_root;

        std::string branch = "unknown";
        fs::path head_path = fs::path(ctx.root_path) / ".git" / "HEAD";
        error.clear();
        if (fs::exists(head_path, error) && !error) {
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
