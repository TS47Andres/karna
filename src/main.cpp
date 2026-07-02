#include <CLI/CLI.hpp>
#include <iostream>
#include <cstdlib>

#include "cli/init.h"
#include "cli/chat.h"
#include "cli/run.h"
#include "cli/config_cmd.h"
#include "cli/mcp_cmd.h"

int main(int argc, char** argv)
{
    CLI::App app{"Karna - AI coding harness for the terminal"};

    app.require_subcommand(0, 1);
    app.set_help_all_flag("--help-all", "Show all help");

    auto* init_cmd = app.add_subcommand("init", "Initialize Karna configuration");
    auto* chat_cmd = app.add_subcommand("chat", "Start an interactive chat session");
    auto* run_cmd = app.add_subcommand("run", "Run a one-shot prompt (non-interactive)");
    auto* config_cmd = app.add_subcommand("config", "View or edit configuration");
    auto* mcp_cmd_app = app.add_subcommand("mcp", "Run as an MCP server");

    std::string prompt;
    run_cmd->add_option("prompt", prompt, "Prompt to process");

    CLI11_PARSE(app, argc, argv);

    if (init_cmd->parsed()) {
        return cli::run_init(argc, argv);
    }
    if (chat_cmd->parsed()) {
        return cli::run_chat(argc, argv);
    }
    if (run_cmd->parsed()) {
        return cli::run_prompt(argc, argv);
    }
    if (config_cmd->parsed()) {
        return cli::run_config(argc, argv);
    }
    if (mcp_cmd_app->parsed()) {
        return cli::run_mcp(argc, argv);
    }

    // Default: run chat
    return cli::run_chat(argc, argv);
}
