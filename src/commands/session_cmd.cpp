#include "commands/session_cmd.h"
#include "core/command.h"
#include "core/session.h"
#include "tui/chat_view.h"
#include <sstream>

std::string SessionCommand::name() const { return "session"; }

std::string SessionCommand::description() const
{
    return "Show session info: model, token usage, message count.";
}

void SessionCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    std::ostringstream info;
    info << "Session info:\n";
    info << "  Model: " << ctx.session.model() << "\n";
    info << "  Messages: " << ctx.session.history().size() << "\n";
    info << "  Tokens: " << ctx.session.total_usage().total_tokens
         << " (P:" << ctx.session.total_usage().prompt_tokens
         << " C:" << ctx.session.total_usage().completion_tokens << ")\n";
    ctx.chat_view.show_system_message(info.str());
    ctx.request_rerender();
}
