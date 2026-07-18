#pragma once

#include "core/tool.h"
#include "config/config.h"

#include <functional>
#include <string>

class SubAgentTool : public Tool {
public:
    using ConfigGetter = std::function<Config()>;
    using ModelGetter = std::function<std::string()>;

    SubAgentTool(ConfigGetter config_getter, ModelGetter model_getter);

    std::string name() const override;
    std::string description() const override;
    json parameters() const override;
    ToolResult execute(const json& params) override;
    ToolResult execute_stream(const json& params, ToolOutputCallback on_output,
                              ToolCancelCallback should_cancel = {}) override;

private:
    ConfigGetter config_getter_;
    ModelGetter model_getter_;
};
