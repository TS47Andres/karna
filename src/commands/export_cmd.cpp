#include "commands/export_cmd.h"
#include "core/session.h"
#include "io/file_ops.h"
#include <sstream>

std::string ExportCommand::name() const { return "export"; }

std::string ExportCommand::description() const
{
    return "Export conversation to a markdown file. Usage: /export [path]";
}

void ExportCommand::execute(const std::string& args, CommandContext& ctx)
{
    std::string path = args.empty() ? "karna-conversation.md" : args;

    std::ostringstream md;
    md << "# Karna Conversation\n\n";
    for (const auto& msg : ctx.session.history()) {
        switch (msg.role) {
            case MessageRole::User:
                md << "## User\n\n" << msg.content << "\n\n";
                break;
            case MessageRole::Assistant:
                md << "## Assistant\n\n" << msg.content << "\n\n";
                break;
            case MessageRole::System:
                md << "## System\n\n" << msg.content << "\n\n";
                break;
            case MessageRole::Tool:
                md << "## Tool (" << (msg.tool_call_id.value_or("")) << ")\n\n" << msg.content << "\n\n";
                break;
        }
    }

    if (FileOps::write_file(path, md.str())) {
        // Success message displayed by controller
    }
}
