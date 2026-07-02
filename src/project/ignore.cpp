#include "project/ignore.h"

#include <filesystem>
#include <fstream>
#include <algorithm>

IgnorePattern::IgnorePattern(std::string pattern)
    : pattern_(std::move(pattern))
{
    if (pattern_.starts_with('!')) {
        is_negation_ = true;
        pattern_ = pattern_.substr(1);
    }
    if (pattern_.ends_with('/')) {
        is_directory_only_ = true;
        pattern_.pop_back();
    }
}

bool IgnorePattern::matches(const std::string& path) const
{
    std::string fname = std::filesystem::path(path).filename().string();

    if (pattern_.find('*') != std::string::npos) {
        size_t star = pattern_.find('*');
        if (star == 0) {
            std::string suffix = pattern_.substr(1);
            if (fname.size() >= suffix.size() &&
                fname.compare(fname.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return !is_negation_;
            }
        }
    }

    if (fname == pattern_) {
        return !is_negation_;
    }

    return false;
}

void IgnoreRules::load_from_file(const std::string& path)
{
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        add_pattern(line);
    }
}

void IgnoreRules::add_pattern(const std::string& pattern)
{
    patterns_.emplace_back(pattern);
}

bool IgnoreRules::is_ignored(const std::string& path) const
{
    bool ignored = false;
    for (const auto& p : patterns_) {
        if (p.matches(path)) {
            ignored = true;
        }
    }
    return ignored;
}

void IgnoreRules::add_default_rules()
{
    add_pattern(".git/");
    add_pattern("node_modules/");
    add_pattern(".karna/");
    add_pattern("build/");
    add_pattern("dist/");
    add_pattern(".DS_Store");
    add_pattern("*.pyc");
    add_pattern("__pycache__/");
    add_pattern(".env");
    add_pattern("venv/");
    add_pattern(".venv/");
    add_pattern("target/");
    add_pattern(".next/");
}
