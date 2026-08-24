#include "commands/delete_cmd.h"

std::string DeleteCommand::name() const { return "delete"; }

std::string DeleteCommand::description() const
{
    return "Delete the current chat session.";
}

void DeleteCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
}
