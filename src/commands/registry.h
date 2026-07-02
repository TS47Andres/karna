#pragma once

#include "core/command.h"
#include "commands/help.h"
#include "commands/clear.h"
#include "commands/model.h"
#include "commands/tokens.h"
#include "commands/skills_list.h"
#include "commands/cost.h"
#include "commands/export_cmd.h"
#include "commands/session_cmd.h"

class CommandInitializer {
public:
    static void register_all(CommandRegistry& registry);
};
