#pragma once

#include "core/command.h"

class NewCommand : public Command {
public:
    std::string name() const override;
    std::string description() const override;
    void execute(const std::string& args, CommandContext& ctx) override;
};
