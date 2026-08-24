#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>

class Session;
class ChatView;
class ToolRegistry;
class CommandRegistry;

struct CommandContext {
    Session& session;
    ChatView& chat_view;
    CommandRegistry* command_registry{nullptr};
    ToolRegistry* tool_registry{nullptr};
    std::function<void()> request_rerender;
    std::function<void(const std::string&)> set_api_key;
    std::function<void(const std::string&)> set_exa_api_key;
};

class Command {
public:
    virtual ~Command() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual void execute(const std::string& args, CommandContext& ctx) = 0;
};

using CommandPtr = std::unique_ptr<Command>;

class CommandRegistry {
public:
    void register_command(CommandPtr command);
    Command* find(const std::string& name) const;
    std::vector<const Command*> all() const;

private:
    std::vector<CommandPtr> commands_;
};
