#include "tools/registry.h"
#include "core/skill.h"
#include "core/command.h"

void ToolRegistry::register_tool(ToolPtr tool)
{
    tools_.push_back(std::move(tool));
}

Tool* ToolRegistry::find(const std::string& name) const
{
    for (const auto& t : tools_) {
        if (t->name() == name) {
            return t.get();
        }
    }
    return nullptr;
}

std::vector<const Tool*> ToolRegistry::all() const
{
    std::vector<const Tool*> result;
    for (const auto& t : tools_) {
        result.push_back(t.get());
    }
    return result;
}

std::vector<std::string> ToolRegistry::all_names() const
{
    std::vector<std::string> names;
    for (const auto& t : tools_) {
        names.push_back(t->name());
    }
    return names;
}

void SkillRegistry::register_skill(SkillPtr skill)
{
    skills_.push_back(std::move(skill));
}

Skill* SkillRegistry::find(const std::string& name) const
{
    for (const auto& s : skills_) {
        if (s->name() == name) {
            return s.get();
        }
    }
    return nullptr;
}

std::vector<const Skill*> SkillRegistry::all() const
{
    std::vector<const Skill*> result;
    for (const auto& s : skills_) {
        result.push_back(s.get());
    }
    return result;
}

std::vector<std::string> SkillRegistry::all_names() const
{
    std::vector<std::string> names;
    for (const auto& s : skills_) {
        names.push_back(s->name());
    }
    return names;
}

void CommandRegistry::register_command(CommandPtr command)
{
    commands_.push_back(std::move(command));
}

Command* CommandRegistry::find(const std::string& name) const
{
    for (const auto& c : commands_) {
        if (c->name() == name) {
            return c.get();
        }
    }
    return nullptr;
}

std::vector<const Command*> CommandRegistry::all() const
{
    std::vector<const Command*> result;
    for (const auto& c : commands_) {
        result.push_back(c.get());
    }
    return result;
}
