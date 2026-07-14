#include "commands/connect.h"
#include "tui/chat_view.h"

#include <algorithm>
#include <cctype>
#include <utility>

ConnectCommand::ConnectCommand(std::string command_name)
    : command_name_(std::move(command_name))
{}

std::string ConnectCommand::name() const
{
    return command_name_;
}

std::string ConnectCommand::description() const
{
    return "Save an API key. Usage: /" + command_name_ + " <api-key>";
}

void ConnectCommand::execute(const std::string& args, CommandContext& ctx)
{
    std::string key = args;
    key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](unsigned char c) {
        return !std::isspace(c);
    }));
    key.erase(std::find_if(key.rbegin(), key.rend(), [](unsigned char c) {
        return !std::isspace(c);
    }).base(), key.end());

    if (key.empty()) {
        ctx.chat_view.show_system_message("Usage: /" + command_name_ + " <api-key>");
        ctx.request_rerender();
        return;
    }

    if (!ctx.set_api_key) {
        ctx.chat_view.show_system_message("API key setup is unavailable.");
        ctx.request_rerender();
        return;
    }

    ctx.set_api_key(key);
    ctx.chat_view.show_system_message("API key saved.");
    ctx.request_rerender();
}
