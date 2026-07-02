#pragma once

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace mcp {

struct ToolDefinition {
    std::string name;
    std::string description;
    json parameters;
};

struct CallRequest {
    std::string tool_name;
    json arguments;
};

struct CallResult {
    bool success;
    std::string output;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(const json& message) = 0;
    virtual void on_message(std::function<void(json)> callback) = 0;
    virtual void close() = 0;
};

class StdioTransport : public Transport {
public:
    explicit StdioTransport(const std::string& command, const std::vector<std::string>& args);
    ~StdioTransport() override;
    void send(const json& message) override;
    void on_message(std::function<void(json)> callback) override;
    void close() override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Client {
public:
    explicit Client(std::unique_ptr<Transport> transport);
    ~Client();

    void connect();
    std::vector<ToolDefinition> list_tools();
    CallResult call_tool(const std::string& name, const json& args);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class Server {
public:
    explicit Server(std::unique_ptr<Transport> transport);
    ~Server();

    void add_tool(const ToolDefinition& tool, std::function<CallResult(json)> handler);
    void start();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}
