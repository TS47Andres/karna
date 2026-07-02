#include "commands/tokens.h"
#include "core/session.h"

std::string TokensCommand::name() const { return "tokens"; }

std::string TokensCommand::description() const
{
    return "Show token usage for the current session.";
}

void TokensCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    Usage usage = ctx.session.total_usage();
    // Display is handled by the controller
}
