#pragma once

#include <string>
#include <optional>
#include <unordered_map>

#include <toml++/toml.h>

struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    std::string default_model;
    int max_tokens{4096};
    double temperature{0.7};
};

struct ExaConfig {
    std::string api_key;
    int num_results{5};
};

struct Config {
    ProviderConfig openrouter;
    ExaConfig exa;
    std::string theme{"default"};
    bool auto_approve_commands{false};
    int max_history_age{100};

    static Config load();
    static Config load_from_file(const std::string& path);

    // All mutable Karna state is scoped to the directory where the app runs.
    static std::string storage_dir();
    static std::string config_path();
    static std::string mcp_config_path();

    void save(const std::string& path = "");

private:
    static Config default_config();
};
