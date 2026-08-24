#include "commands/cost.h"
#include "core/session.h"
#include "tui/chat_view.h"
#include <iomanip>
#include <sstream>

std::string CostCommand::name() const { return "cost"; }

std::string CostCommand::description() const
{
    return "Show estimated cost for the current session based on token usage.";
}

void CostCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    const Usage usage = ctx.session.total_usage();
    std::ostringstream out;
    out << "Current session cost: "
        << std::fixed << std::setprecision(6) << usage.cost << " credits\n";
    out << "Tokens: " << usage.total_tokens
        << " (P:" << usage.prompt_tokens
        << " C:" << usage.completion_tokens << ")";
    ctx.chat_view.show_system_message(out.str());
    ctx.request_rerender();
}
