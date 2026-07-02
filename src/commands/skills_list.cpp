#include "commands/skills_list.h"

std::string SkillsListCommand::name() const { return "skills"; }

std::string SkillsListCommand::description() const
{
    return "List all available skills.";
}

void SkillsListCommand::execute(const std::string& /*args*/, CommandContext& /*ctx*/)
{
    // Display is handled by the controller
}
