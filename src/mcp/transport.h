#pragma once

#include "core/mcp.h"
#include <vector>
#include <thread>

namespace mcp {

class StdioTransport::Impl {
public:
    Impl();
    ~Impl();

    void send(const json& message);
    void on_message(std::function<void(json)> callback);
    void close();

private:
    std::function<void(json)> callback_;
    std::thread reader_thread_;
    bool running_{false};
};

}

namespace mcp {

class Server::Impl {
public:
    explicit Impl(std::unique_ptr<Transport> transport);
    void add_tool(const ToolDefinition& tool, std::function<CallResult(json)> handler);
    void start();

private:
    std::unique_ptr<Transport> transport_;
    std::vector<std::pair<ToolDefinition, std::function<CallResult(json)>>> tools_;
    void handle_request(const json& msg);
};

}
