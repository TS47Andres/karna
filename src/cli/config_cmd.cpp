#include "cli/config_cmd.h"
#include "config/config.h"

#include <iostream>
#include <fstream>
#include <cstdlib>

int cli::run_config(int /*argc*/, char** /*argv*/)
{
    Config cfg = Config::load();

    std::cout << "OpenRouter API key: " << (cfg.openrouter.api_key.empty() ? "(not set)" : "****" + cfg.openrouter.api_key.substr(std::min(4, (int)cfg.openrouter.api_key.size() - 4))) << std::endl;
    std::cout << "Default model: " << cfg.openrouter.default_model << std::endl;
    std::cout << "Exa AI API key: " << (cfg.exa.api_key.empty() ? "(not set)" : "****" + cfg.exa.api_key.substr(std::min(4, (int)cfg.exa.api_key.size() - 4))) << std::endl;
    std::cout << "Theme: " << cfg.theme << std::endl;

    return 0;
}
