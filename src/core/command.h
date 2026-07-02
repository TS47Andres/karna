#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>

class Session;
class ChatView;

struct CommandContext {
    Session& session;
    ChatView& chat_view;
    std::function<void()> request_rerender;
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
