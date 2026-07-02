#include "commands/skills_list.h"
#include "core/command.h"
#include "core/skill.h"
#include "tui/chat_view.h"
#include <sstream>

std::string SkillsListCommand::name() const { return "skills"; }

std::string SkillsListCommand::description() const
{
    return "List all available skills.";
}

void SkillsListCommand::execute(const std::string& /*args*/, CommandContext& ctx)
{
    if (!ctx.skill_registry) return;
    std::ostringstream skills;
    skills << "Available skills:\n";
    for (const auto* s : ctx.skill_registry->all()) {
        skills << "  " << s->name() << " - " << s->description() << "\n";
    }
    ctx.chat_view.show_system_message(skills.str());
    ctx.request_rerender();
}
