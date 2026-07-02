#pragma once

#include "core/mcp.h"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace mcp {

class StdioTransport::Impl {
public:
    Impl(const std::string& command, const std::vector<std::string>& args);
    ~Impl();

    void send(const json& message);
    void on_message(std::function<void(json)> callback);
    void close();

private:
    std::string command_;
    std::vector<std::string> args_;
    std::function<void(json)> callback_;
    void* process_handle_{nullptr};
    std::thread reader_thread_;
    bool running_{false};
};

}

namespace mcp {

class Client::Impl {
public:
    explicit Impl(std::unique_ptr<Transport> transport);
    std::vector<ToolDefinition> list_tools();
    CallResult call_tool(const std::string& name, const json& args);
    void connect();

private:
    std::unique_ptr<Transport> transport_;
    int request_id_{1};
    std::mutex mutex_;
    std::condition_variable cv_;
    json pending_response_;
    bool response_ready_{false};
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
