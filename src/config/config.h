#pragma once

#include <string>
#include <toml++/toml.h>

struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    std::string default_model;
};

struct ExaConfig {
    std::string api_key;
    int num_results{5};
};

struct Config {
    ProviderConfig openrouter;
    ExaConfig exa;

    static Config load();
    static Config load_from_file(const std::string& path);

    // All mutable Karna state is scoped to the directory where the app runs.
    static std::string storage_dir();
    static std::string config_path();
    void save(const std::string& path = "");

private:
    static Config default_config();
};
