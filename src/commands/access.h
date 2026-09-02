#pragma once

#include "core/command.h"

// Access is handled by Controller because changing it is a controller-level
// operation. The command still owns its metadata and fixed autocomplete values.
class AccessCommand : public Command {
public:
    std::string name() const override;
    std::string description() const override;
    void execute(const std::string& args, CommandContext& ctx) override;
    std::vector<CommandAutocompleteOption> autocomplete_options() const override;
};
