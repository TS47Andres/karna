#pragma once
#include "config/config.h"
#include "core/tool.h"
#include "core/message.h"
#include <functional>
#include <string>

enum class AccessDecision { Allow, Deny };

// Policy boundary for main-agent calls only. Sub-agent registries never use this.
class AccessController {
public:
    using Confirm = std::function<AccessDecision(const std::string&)>;
    using Auto = std::function<AccessDecision(const std::string&, const std::string&, const std::string&)>;
    AccessController(std::string mode, Confirm confirm, Auto automatic);
    AccessDecision decide(const ToolCall& call, const std::string& model);
    static std::string normalize_mode(std::string mode);
    static std::string classify(const ToolCall& call);
private:
    std::string mode_; Confirm confirm_; Auto automatic_;
};
