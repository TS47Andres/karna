#pragma once

#include "core/tool.h"

class RunTool : public Tool {
public:
    std::string name() const override;
    std::string description() const override;
    json parameters() const override;
    ToolResult execute(const json& params) override;
    ToolResult execute_stream(const json& params, ToolOutputCallback on_output,
                              ToolCancelCallback should_cancel = {}) override;
};
