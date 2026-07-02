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
};

enum class FinishReason {
    Stop,
    Length,
    ToolCalls,
    Error,
    Unknown
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
    std::optional<ToolCall> tool_call;
    std::optional<std::string> tool_call_id;
};

struct StreamEvent {
    enum class Type {
        Delta,
        Done,
        Error
    };
    Type type;
    std::optional<Delta> delta;
    std::optional<std::string> error_message;
};

struct Usage {
    int prompt_tokens{0};
    int completion_tokens{0};
    int total_tokens{0};
};
