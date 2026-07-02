#include "commands/help.h"
#include "core/command.h"
#include "core/session.h"
#include "core/tool.h"
#include "core/skill.h"
#include "commands/registry.h"
#include "tui/chat_view.h"
#include <sstream>

std::string HelpCommand::name() const { return "help"; }

std::string HelpCommand::description() const
{
    return "Show available slash commands and their descriptions.";
}

void HelpCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    std::ostringstream help;
    help << "Available commands:\n";
    for (const auto* cmd : ctx.command_registry->all()) {
        help << "  /" << cmd->name() << " - " << cmd->description() << "\n";
    }
    if (ctx.tool_registry) {
        help << "\nAvailable tools:\n";
        for (const auto* t : ctx.tool_registry->all()) {
            help << "  " << t->name() << " - " << t->description() << "\n";
        }
    }
    if (ctx.skill_registry) {
        help << "\nAvailable skills:\n";
        for (const auto* s : ctx.skill_registry->all()) {
            help << "  /" << s->name() << " - " << s->description() << "\n";
        }
    }
    ctx.chat_view.show_system_message(help.str());
    ctx.request_rerender();
}
