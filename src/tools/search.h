#pragma once

#include "core/tool.h"
#include "config/config.h"

class SearchTool : public Tool {
public:
    explicit SearchTool(ExaConfig config);

    std::string name() const override;
    std::string description() const override;
    json parameters() const override;
    ToolResult execute(const json& params) override;

private:
    ExaConfig config_;
    ToolResult perform_exa_search(const std::string& query, int num_results) const;
};
