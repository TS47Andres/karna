#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>

#include "core/message.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ModelInfo {
    std::string id;
    std::string name;
    std::string owned_by;
    int context_length{0};
};

class Provider {
public:
    virtual ~Provider() = default;

    virtual std::string name() const = 0;

    virtual void send(
        const std::vector<Message>& history,
        const json& tools,
        const std::string& model,
        std::function<void(Delta)> on_delta,
        std::function<void(std::string)> on_error,
        std::function<void(Usage)> on_done
    ) = 0;

    virtual void abort() = 0;

    virtual void set_model(const std::string& /*model*/) {}

    virtual void set_api_key(const std::string& /*api_key*/) {}

    virtual std::vector<ModelInfo> available_models() const { return {}; }

    virtual int context_window() const { return 0; }

    virtual int count_tokens(const std::string& text) const = 0;

    virtual std::string model() const = 0;
};

using ProviderPtr = std::unique_ptr<Provider>;
