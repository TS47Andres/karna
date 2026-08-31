#include "config/config.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

Config Config::default_config()
{
    Config cfg;
    cfg.openrouter.base_url = "https://openrouter.ai/api/v1";
    cfg.openrouter.default_model = "deepseek/deepseek-v4-flash";
    cfg.openrouter.api_key = "";
    cfg.exa.api_key = "";
    cfg.exa.num_results = 5;
    cfg.access_mode = "confirm";
    return cfg;
}

std::string Config::config_path()
{
    return (fs::path(storage_dir()) / "config.toml").string();
}

std::string Config::storage_dir()
{
    return (fs::current_path() / ".karna").string();
}

Config Config::load()
{
    std::string path = config_path();
    Config cfg = fs::exists(path) ? load_from_file(path) : default_config();

    return cfg;
}

Config Config::load_from_file(const std::string& path)
{
    Config cfg = default_config();

    try {
        auto tbl = toml::parse_file(path);
        if (auto* mode = tbl.get("access_mode"); mode && mode->is_string()) cfg.access_mode = mode->value_or("confirm");

        if (auto* openrouter = tbl["openrouter"].as_table()) {
            if (auto* key = openrouter->get("api_key")) {
                cfg.openrouter.api_key = key->as_string()->get();
            }
            if (auto* url = openrouter->get("base_url")) {
                cfg.openrouter.base_url = url->as_string()->get();
            }
            if (auto* model = openrouter->get("default_model")) {
                cfg.openrouter.default_model = model->as_string()->get();
            }
        }

        if (auto* exa = tbl["exa"].as_table()) {
            if (auto* key = exa->get("api_key")) {
                cfg.exa.api_key = key->as_string()->get();
            }
            if (auto* nr = exa->get("num_results")) {
                cfg.exa.num_results = static_cast<int>(nr->as_integer()->get());
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Warning: failed to parse config: " << e.what() << std::endl;
    }

    return cfg;
}

void Config::save(const std::string& path)
{
    std::string save_path = path.empty() ? config_path() : path;
    fs::path parent = fs::path(save_path).parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent);
    }

    toml::table tbl;
    tbl.emplace("access_mode", access_mode);

    toml::table or_tbl;
    or_tbl.emplace("api_key", openrouter.api_key);
    or_tbl.emplace("base_url", openrouter.base_url);
    or_tbl.emplace("default_model", openrouter.default_model);
    tbl.emplace("openrouter", or_tbl);

    toml::table exa_tbl;
    exa_tbl.emplace("api_key", exa.api_key);
    exa_tbl.emplace("num_results", exa.num_results);
    tbl.emplace("exa", exa_tbl);

    std::ofstream file(save_path);
    if (file) {
        file << tbl << std::endl;
    }

}
