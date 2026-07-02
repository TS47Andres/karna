#pragma once

#include "core/skill.h"
#include "skills/explain.h"
#include "skills/fix_bug.h"
#include "skills/add_feature.h"

class SkillInitializer {
public:
    static void register_all(SkillRegistry& registry);
};
