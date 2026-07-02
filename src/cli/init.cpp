#include "cli/init.h"
#include "config/config.h"

#include <iostream>
#include <cstdlib>

int cli::run_init(int /*argc*/, char** /*argv*/)
{
    Config cfg = Config::load();

    const char* or_key = std::getenv("OPENROUTER_API_KEY");
    if (or_key) {
        cfg.openrouter.api_key = or_key;
    }

    const char* exa_key = std::getenv("EXA_API_KEY");
    if (exa_key) {
        cfg.exa.api_key = exa_key;
    }

    cfg.save();
    std::cout << "Configuration initialized." << std::endl;
    return 0;
}
