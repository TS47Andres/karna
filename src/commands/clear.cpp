#include "commands/clear.h"
#include "core/session.h"
#include "tui/chat_view.h"

std::string ClearCommand::name() const { return "clear"; }

std::string ClearCommand::description() const
{
    return "Clear the conversation history.";
}

void ClearCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    ctx.session.clear();
    ctx.chat_view.clear();
}
