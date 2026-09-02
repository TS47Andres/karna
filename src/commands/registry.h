#pragma once

#include "core/command.h"
#include "commands/help.h"
#include "commands/clear.h"
#include "commands/model.h"
#include "commands/cost.h"
#include "commands/export_cmd.h"
#include "commands/session_cmd.h"
#include "commands/sessions_cmd.h"
#include "commands/new_cmd.h"
#include "commands/delete_cmd.h"
#include "commands/connect.h"
#include "commands/access.h"

class CommandInitializer {
public:
    static void register_all(CommandRegistry& registry);
};
