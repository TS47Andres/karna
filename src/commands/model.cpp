#include "commands/model.h"
#include "core/session.h"

std::string ModelCommand::name() const { return "model"; }

std::string ModelCommand::description() const
{
    return "Set the active model. Usage: /model <model-name>";
}

void ModelCommand::execute(const std::string& args, CommandContext& ctx)
{
    if (args.empty()) {
        return;
    }
    ctx.session.set_model(args);
}
