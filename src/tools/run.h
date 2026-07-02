#pragma once

#include "core/tool.h"

class RunTool : public Tool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters() const override;
    ToolResult execute(const json& params) override;
};
