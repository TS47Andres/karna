#include "commands/access.h"

std::string AccessCommand::name() const
{
    return "access";
}

std::string AccessCommand::description() const
{
    return "Change tool access mode (full, confirm, or auto).";
}

void AccessCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
    // Controller handles /access before dispatching registry commands.
}

std::vector<CommandAutocompleteOption> AccessCommand::autocomplete_options() const
{
    return {
        {"full", "Allow tool calls without prompting."},
        {"confirm", "Ask before running tool calls."},
        {"auto", "Choose automatically based on the tool and request."},
    };
}
