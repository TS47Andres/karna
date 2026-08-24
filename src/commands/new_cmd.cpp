#include "commands/new_cmd.h"

std::string NewCommand::name() const { return "new"; }

std::string NewCommand::description() const
{
    return "Create and switch to a new chat session.";
}

void NewCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
    // Controller owns session lifetimes and updates the TUI after switching.
}
