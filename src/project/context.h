#pragma once

#include <string>
#include <vector>
#include <optional>

struct ProjectContext {
    std::string root_path;
    std::vector<std::string> tracked_files;
    bool has_git{false};
    std::string git_branch;
    std::vector<std::string> git_changed_files;

    static ProjectContext discover(const std::string& start_path = ".");
    static std::optional<std::string> find_git_root(const std::string& path);

    std::string summarize() const;
};
