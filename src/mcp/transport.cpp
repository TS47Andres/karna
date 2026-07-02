#include "mcp/transport.h"

#include <cstdio>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace mcp {

StdioTransport::StdioTransport(const std::string& command, const std::vector<std::string>& args)
    : impl_(std::make_unique<Impl>(command, args))
{}

StdioTransport::~StdioTransport() = default;

void StdioTransport::send(const json& message) { impl_->send(message); }
void StdioTransport::on_message(std::function<void(json)> callback) { impl_->on_message(std::move(callback)); }
void StdioTransport::close() { impl_->close(); }

StdioTransport::Impl::Impl(const std::string& command, const std::vector<std::string>& args)
    : command_(command), args_(args)
{}

StdioTransport::Impl::~Impl() { close(); }

void StdioTransport::Impl::send(const json& message)
{
    std::string msg_str = message.dump() + "\n";
    fwrite(msg_str.data(), 1, msg_str.size(), stdout);
    fflush(stdout);
}

void StdioTransport::Impl::on_message(std::function<void(json)> callback)
{
    callback_ = std::move(callback);
    running_ = true;
    reader_thread_ = std::thread([this]() {
        std::string buf;
        char ch;
        while (running_ && fread(&ch, 1, 1, stdin) == 1) {
            if (ch == '\n') {
                if (!buf.empty()) {
                    try {
                        auto j = json::parse(buf);
                        if (callback_) callback_(j);
                    } catch (...) {}
                }
                buf.clear();
            } else {
                buf += ch;
            }
        }
    });
}

void StdioTransport::Impl::close()
{
    running_ = false;
    if (reader_thread_.joinable()) reader_thread_.join();
}


Client::Client(std::unique_ptr<Transport> transport)
    : impl_(std::make_unique<Impl>(std::move(transport)))
{}

Client::~Client() = default;

void Client::connect() { impl_->connect(); }
std::vector<ToolDefinition> Client::list_tools() { return impl_->list_tools(); }
CallResult Client::call_tool(const std::string& name, const json& args) { return impl_->call_tool(name, args); }

Client::Impl::Impl(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport))
{}

void Client::Impl::connect()
{
    transport_->on_message([this](const json& msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_response_ = msg;
        response_ready_ = true;
        cv_.notify_one();
    });
}

std::vector<ToolDefinition> Client::Impl::list_tools()
{
    json req;
    req["jsonrpc"] = "2.0";
    req["id"] = request_id_++;
    req["method"] = "tools/list";
    req["params"] = json::object();

    response_ready_ = false;
    transport_->send(req);

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::seconds(10), [this] { return response_ready_; });

    if (!response_ready_) return {};

    std::vector<ToolDefinition> tools;
    if (pending_response_.contains("result") && pending_response_["result"].contains("tools")) {
        for (const auto& t : pending_response_["result"]["tools"]) {
            tools.push_back({
                t["name"].get<std::string>(),
                t.value("description", ""),
                t.value("parameters", json::object())
            });
        }
    }
    return tools;
}

CallResult Client::Impl::call_tool(const std::string& name, const json& args)
{
    json req;
    req["jsonrpc"] = "2.0";
    req["id"] = request_id_++;
    req["method"] = "tools/call";
    req["params"] = {{"name", name}, {"arguments", args}};

    response_ready_ = false;
    transport_->send(req);

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::seconds(60), [this] { return response_ready_; });

    if (!response_ready_) return {false, "Request timed out"};
    if (pending_response_.contains("error")) return {false, pending_response_["error"].value("message", "Unknown error")};
    if (pending_response_.contains("result")) return {true, pending_response_["result"].value("output", "")};

    return {false, "Invalid response"};
}


Server::Server(std::unique_ptr<Transport> transport)
    : impl_(std::make_unique<Impl>(std::move(transport)))
{}

Server::~Server() = default;

void Server::add_tool(const ToolDefinition& tool, std::function<CallResult(json)> handler) { impl_->add_tool(tool, std::move(handler)); }
void Server::start() { impl_->start(); }

Server::Impl::Impl(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport))
{}

void Server::Impl::add_tool(const ToolDefinition& tool, std::function<CallResult(json)> handler)
{
    tools_.emplace_back(tool, std::move(handler));
}

void Server::Impl::start()
{
    transport_->on_message([this](const json& msg) { handle_request(msg); });
}

void Server::Impl::handle_request(const json& msg)
{
    if (!msg.contains("method") || !msg.contains("id")) return;

    std::string method = msg["method"];
    int id = msg["id"].get<int>();

    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;

    if (method == "tools/list") {
        json tools = json::array();
        for (const auto& [def, _] : tools_) {
            json t;
            t["name"] = def.name;
            t["description"] = def.description;
            t["parameters"] = def.parameters;
            tools.push_back(t);
        }
        response["result"] = {{"tools", tools}};
    } else if (method == "tools/call") {
        std::string name = msg["params"]["name"];
        json args = msg["params"]["arguments"];
        for (const auto& [def, handler] : tools_) {
            if (def.name == name) {
                auto result = handler(args);
                response["result"] = {{"output", result.output}};
                break;
            }
        }
    } else {
        response["error"] = {{"code", -32601}, {"message", "Method not found"}};
    }

    transport_->send(response);
}

}
