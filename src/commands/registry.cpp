#include "commands/registry.h"

void CommandInitializer::register_all(CommandRegistry& registry)
{
    registry.register_command(std::make_unique<HelpCommand>());
    registry.register_command(std::make_unique<ClearCommand>());
    registry.register_command(std::make_unique<ModelCommand>());
    registry.register_command(std::make_unique<TokensCommand>());
    registry.register_command(std::make_unique<CostCommand>());
    registry.register_command(std::make_unique<ExportCommand>());
    registry.register_command(std::make_unique<SessionCommand>());
    registry.register_command(std::make_unique<ConnectCommand>("connect"));
    registry.register_command(std::make_unique<ConnectCommand>("setup"));
    registry.register_command(std::make_unique<ConnectCommand>("connect-exa", true));
}
