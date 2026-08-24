#include "commands/sessions_cmd.h"

std::string SessionsCommand::name() const { return "sessions"; }

std::string SessionsCommand::description() const
{
    return "List, create, and switch persistent chat sessions.";
}

void SessionsCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
    // Controller owns session lifetimes because switching must also update the TUI.
}
