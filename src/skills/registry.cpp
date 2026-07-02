#include "skills/registry.h"

void SkillInitializer::register_all(SkillRegistry& registry)
{
    registry.register_skill(std::make_unique<ExplainSkill>());
    registry.register_skill(std::make_unique<FixBugSkill>());
    registry.register_skill(std::make_unique<AddFeatureSkill>());
}
