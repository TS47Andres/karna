#pragma once

#include <string>
#include <vector>
#include <memory>
#include <ctime>

#include "core/message.h"
#include "core/provider.h"
#include "config/config.h"

struct SessionData {
    std::string id;
    std::string title;
    std::string created_at;
    std::string updated_at;
    std::string model;
    std::vector<Message> history;
    Usage usage;
    int context_usage{0};
};

class Session {
public:
    explicit Session(const Config& config, std::string id = "", std::string title = "");

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

    const std::string& id() const;
    const std::string& title() const;
    const std::string& updated_at() const;
    SessionData snapshot() const;
    void restore(const SessionData& data);

private:
    std::vector<Message> history_;
    ProviderPtr provider_;
    std::string model_;
    Usage total_usage_;
    int context_usage_{0};
    std::string id_;
    std::string title_;
    std::string created_at_;
    std::string updated_at_;
};
