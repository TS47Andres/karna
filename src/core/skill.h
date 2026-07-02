#pragma once

#include "core/tool.h"

class Skill : public Tool {
public:
    ~Skill() override = default;
};

using SkillPtr = std::unique_ptr<Skill>;

class SkillRegistry {
public:
    void register_skill(SkillPtr skill);
    Skill* find(const std::string& name) const;
    std::vector<const Skill*> all() const;
    std::vector<std::string> all_names() const;

private:
    std::vector<SkillPtr> skills_;
};
