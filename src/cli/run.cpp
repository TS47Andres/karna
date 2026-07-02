#include "cli/run.h"
#include "controller.h"

#include <iostream>

int cli::run_prompt(int /*argc*/, char** /*argv*/)
{
    std::cout << "One-shot mode not yet implemented in the TUI. Use 'karna chat' for interactive mode." << std::endl;
    return 1;
}
