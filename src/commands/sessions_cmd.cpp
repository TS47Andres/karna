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

std::vector<CommandAutocompleteOption> SessionsCommand::autocomplete_options() const
{
    return {
        {"list", "Show saved chat sessions."},
        {"refresh", "Refresh the session list."},
        {"next", "Switch to the next session."},
        {"prev", "Switch to the previous session."},
    };
}
