#include "commands/session_cmd.h"

std::string SessionCommand::name() const { return "session"; }

std::string SessionCommand::description() const
{
    return "Show session info: model, token usage, message count.";
}

void SessionCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
    // Display is handled by the controller
}
