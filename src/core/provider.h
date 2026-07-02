#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>

#include "core/message.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class Provider {
public:
    virtual ~Provider() = default;

    virtual std::string name() const = 0;

    virtual void send(
        const std::vector<Message>& history,
        const json& tools,
        std::function<void(Delta)> on_delta,
        std::function<void(std::string)> on_error,
        std::function<void(Usage)> on_done
    ) = 0;

    virtual void abort() = 0;

    virtual int count_tokens(const std::string& text) const = 0;

    virtual std::string model() const = 0;
};

using ProviderPtr = std::unique_ptr<Provider>;
