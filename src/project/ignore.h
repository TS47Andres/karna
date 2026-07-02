#pragma once

#include <string>
#include <vector>

class IgnorePattern {
public:
    explicit IgnorePattern(std::string pattern);
    bool matches(const std::string& path) const;

private:
    std::string pattern_;
    bool is_negation_{false};
    bool is_directory_only_{false};
};

class IgnoreRules {
public:
    void load_from_file(const std::string& path);
    void add_pattern(const std::string& pattern);
    bool is_ignored(const std::string& path) const;
    void add_default_rules();

private:
    std::vector<IgnorePattern> patterns_;
};
