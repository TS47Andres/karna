#include "mcp/transport.h"

#include <cstdio>
#include <thread>

namespace mcp {

StdioTransport::StdioTransport()
    : impl_(std::make_unique<Impl>())
{}

StdioTransport::~StdioTransport() = default;

void StdioTransport::send(const json& message) { impl_->send(message); }
void StdioTransport::on_message(std::function<void(json)> callback) { impl_->on_message(std::move(callback)); }
void StdioTransport::close() { impl_->close(); }

StdioTransport::Impl::Impl() = default;

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
