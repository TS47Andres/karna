#include "access_control.h"
#include <algorithm>
#include <cctype>
#include <utility>

AccessController::AccessController(std::string mode, Confirm confirm, Auto automatic)
 : mode_(normalize_mode(std::move(mode))), confirm_(std::move(confirm)), automatic_(std::move(automatic)) {}
std::string AccessController::normalize_mode(std::string mode) {
 std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c){ return (char)std::tolower(c); });
 return mode == "full" || mode == "auto" || mode == "confirm" ? mode : "confirm";
}
std::string AccessController::classify(const ToolCall& call) {
 if (call.function_name=="read" || call.function_name=="glob" || call.function_name=="grep" || call.function_name=="search") return "inspection";
 return call.function_name=="sub_agent" ? "sub-agent spawn" : "write or command";
}
AccessDecision AccessController::decide(const ToolCall& call, const std::string& model) {
 if (mode_=="full") return AccessDecision::Allow;
 const std::string detail = call.function_name + " [" + classify(call) + "]\n" + (call.arguments.empty()?"{}":call.arguments);
 if (mode_=="confirm") return confirm_ ? confirm_(detail) : AccessDecision::Deny;
 return automatic_ ? automatic_(call.function_name, detail, model) : AccessDecision::Deny;
}
