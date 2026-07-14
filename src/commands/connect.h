#pragma once

#include "core/command.h"

class ConnectCommand : public Command {
public:
    explicit ConnectCommand(std::string command_name);

    std::string name() const override;
    std::string description() const override;
    void execute(const std::string& args, CommandContext& ctx) override;

private:
    std::string command_name_;
};
