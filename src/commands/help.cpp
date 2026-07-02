#include "commands/help.h"
#include "core/command.h"
#include <sstream>

std::string HelpCommand::name() const { return "help"; }

std::string HelpCommand::description() const
{
    return "Show available slash commands and their descriptions.";
}

void HelpCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
    // The controller handles this by listing all commands
}
