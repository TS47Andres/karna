#include "cli/mcp_cmd.h"
#include "core/mcp.h"
#include "config/config.h"
#include "tools/registry.h"
#include "tools/read.h"
#include "tools/write.h"
#include "tools/edit.h"
#include "tools/bash.h"
#include "tools/sub_agent.h"
#include "tools/search.h"
#include "tools/glob.h"
#include "tools/grep.h"

#include <iostream>
#include <memory>

int cli::run_mcp(int /*argc*/, char** /*argv*/)
{
    Config config = Config::load();

    ToolRegistry tools;
    tools.register_tool(std::make_unique<ReadTool>());
    tools.register_tool(std::make_unique<WriteTool>());
    tools.register_tool(std::make_unique<EditTool>());
    tools.register_tool(std::make_unique<BashTool>());
    tools.register_tool(std::make_unique<SubAgentTool>(
        [config]() { return config; },
        [config]() { return config.openrouter.default_model; }
    ));
    tools.register_tool(std::make_unique<SearchTool>(config.exa));
    tools.register_tool(std::make_unique<GlobTool>());
    tools.register_tool(std::make_unique<GrepTool>());

    auto transport = std::make_unique<mcp::StdioTransport>("", std::vector<std::string>());
    mcp::Server server(std::move(transport));

    for (const auto* t : tools.all()) {
        mcp::ToolDefinition def;
        def.name = t->name();
        def.description = t->description();
        def.parameters = t->parameters();

        server.add_tool(def, [t](const json& args) -> mcp::CallResult {
            auto result = const_cast<Tool*>(t)->execute(args);
            return {result.success, result.output};
        });
    }

    std::cerr << "Karna MCP server started." << std::endl;
    server.start();

    return 0;
}
