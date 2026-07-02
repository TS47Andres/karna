#pragma once

#include "core/skill.h"

class ExplainSkill : public Skill {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters() const override;
    ToolResult execute(const json& params) override;
};
