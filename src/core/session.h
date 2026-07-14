#pragma once

#include <string>
#include <vector>
#include <memory>

#include "core/message.h"
#include "core/provider.h"
#include "config/config.h"

class Session {
public:
    explicit Session(const Config& config);

    void add_message(const Message& msg);
    void clear();
    void set_model(const std::string& model);
    void set_provider(ProviderPtr provider);

    const std::vector<Message>& history() const;
    Provider* provider() const;
    const std::string& model() const;
    Usage total_usage() const;
    void add_usage(const Usage& usage);
    int context_usage() const;
    void set_context_usage(int tokens);
    int estimate_context_usage(const Provider& provider) const;

private:
    std::vector<Message> history_;
    ProviderPtr provider_;
    std::string model_;
    Usage total_usage_;
    int context_usage_{0};
    int max_history_tokens_;
};
