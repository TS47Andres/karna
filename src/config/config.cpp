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
    cfg.openrouter.default_model = "openai/gpt-4o";
    cfg.openrouter.max_tokens = 4096;
    cfg.openrouter.temperature = 0.7;
    cfg.openrouter.api_key = "";
    cfg.exa.api_key = "";
    cfg.exa.num_results = 5;
    cfg.theme = "default";
    cfg.auto_approve_commands = false;
    cfg.max_history_age = 100;
    return cfg;
}

std::string Config::config_path()
{
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    std::string dir = appdata ? std::string(appdata) + "/karna" : fs::current_path().string() + "/.karna";
#else
    const char* home = std::getenv("HOME");
    std::string dir = home ? std::string(home) + "/.config/karna" : fs::current_path().string() + "/.karna";
#endif
    return dir + "/config.toml";
}

Config Config::load()
{
    std::string path = config_path();
    if (fs::exists(path)) {
        return load_from_file(path);
    }
    return default_config();
}

Config Config::load_from_file(const std::string& path)
{
    Config cfg = default_config();

    try {
        auto tbl = toml::parse_file(path);

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
            if (auto* mt = openrouter->get("max_tokens")) {
                cfg.openrouter.max_tokens = static_cast<int>(mt->as_integer()->get());
            }
            if (auto* temp = openrouter->get("temperature")) {
                cfg.openrouter.temperature = temp->as_floating_point()->get();
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

        if (auto* theme = tbl["theme"].as_string()) {
            cfg.theme = theme->get();
        }
        if (auto* aa = tbl["auto_approve_commands"].as_boolean()) {
            cfg.auto_approve_commands = aa->get();
        }
        if (auto* mha = tbl["max_history_age"].as_integer()) {
            cfg.max_history_age = static_cast<int>(mha->get());
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: failed to parse config: " << e.what() << std::endl;
    }

    return cfg;
}

void Config::save(const std::string& path)
{
    std::string save_path = path.empty() ? config_path() : path;

    fs::create_directories(fs::path(save_path).parent_path());

    toml::table tbl;
    tbl.emplace("theme", theme);
    tbl.emplace("auto_approve_commands", auto_approve_commands);
    tbl.emplace("max_history_age", max_history_age);

    toml::table or_tbl;
    or_tbl.emplace("api_key", openrouter.api_key);
    or_tbl.emplace("base_url", openrouter.base_url);
    or_tbl.emplace("default_model", openrouter.default_model);
    or_tbl.emplace("max_tokens", openrouter.max_tokens);
    or_tbl.emplace("temperature", openrouter.temperature);
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
