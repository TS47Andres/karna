#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>

enum class MessageRole {
    System,
    User,
    Assistant,
    Tool
};

struct ToolCall {
    std::string id;
    std::string type;
    std::string function_name;
    std::string arguments;
    int index{-1};
};

enum class FinishReason {
    Stop,
    Length,
    ToolCalls,
    Error,
    Unknown
};

enum class MessageDisplayKind {
    Activity,
    Diff,
    Panel,
    SubAgent
};

struct MessageDisplay {
    MessageDisplayKind kind{MessageDisplayKind::Activity};
    std::string tool_name;
    std::string label;
    std::string parameter;
    std::string extra;
    std::string before;
    std::string after;
    std::string task;
    std::string mode;
    std::string latest_tool;
    std::string transcript;
    int tools_used{0};
    bool success{false};
};

struct Delta {
    std::optional<std::string> content;
    std::optional<ToolCall> tool_call;
    std::optional<FinishReason> finish_reason;
};

struct Message {
    MessageRole role;
    std::string content;
    std::optional<std::string> name;
    std::vector<ToolCall> tool_calls;
    std::optional<std::string> tool_call_id;
    std::optional<MessageDisplay> display;
};

struct Usage {
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
    double cost{0.0};
};
