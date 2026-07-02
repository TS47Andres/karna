#pragma once

#include <string>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ToolResult {
    bool success;
    std::string output;
    json data;

    static ToolResult ok(const std::string& output, json data = json::object()) {
        return {true, output, std::move(data)};
    }

    static ToolResult fail(const std::string& error) {
        return {false, error, json::object()};
    }
};

class Tool {
public:
    virtual ~Tool() = default;

    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters() const = 0;
    virtual ToolResult execute(const json& params) = 0;
};

using ToolPtr = std::unique_ptr<Tool>;

class ToolRegistry {
public:
    void register_tool(ToolPtr tool);
    Tool* find(const std::string& name) const;
    std::vector<const Tool*> all() const;
    std::vector<std::string> all_names() const;

private:
    std::vector<ToolPtr> tools_;
};
