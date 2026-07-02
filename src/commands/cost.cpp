#include "commands/cost.h"
#include "core/session.h"
#include <sstream>

std::string CostCommand::name() const { return "cost"; }

std::string CostCommand::description() const
{
    return "Show estimated cost for the current session based on token usage.";
}

void CostCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    Usage usage = ctx.session.total_usage();
    // Display is handled by the controller
}
