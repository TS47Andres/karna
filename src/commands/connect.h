#pragma once

#include "core/command.h"

class ConnectCommand : public Command {
public:
    explicit ConnectCommand(std::string command_name, bool exa_key = false);

    std::string name() const override;
    std::string description() const override;
    void execute(const std::string& args, CommandContext& ctx) override;

private:
    std::string command_name_;
    bool exa_key_{false};
};
