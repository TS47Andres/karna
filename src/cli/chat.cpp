#include "cli/chat.h"
#include "controller.h"

int cli::run_chat(int /*argc*/, char** /*argv*/)
{
    Controller controller;
    controller.run_chat();
    return 0;
}
