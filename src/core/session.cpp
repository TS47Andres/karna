#include "core/session.h"

Session::Session(const Config& config)
    : model_(config.openrouter.default_model)
    , max_history_tokens_(config.max_history_age)
{
}

void Session::add_message(const Message& msg)
{
    history_.push_back(msg);
}

void Session::clear()
{
    history_.clear();
}

void Session::set_model(const std::string& model)
{
    model_ = model;
}

void Session::set_provider(ProviderPtr provider)
{
    provider_ = std::move(provider);
}

const std::vector<Message>& Session::history() const
{
    return history_;
}

Provider* Session::provider() const
{
    return provider_.get();
}

const std::string& Session::model() const
{
    return model_;
}

Usage Session::total_usage() const
{
    return total_usage_;
}

void Session::add_usage(const Usage& usage)
{
    total_usage_.prompt_tokens += usage.prompt_tokens;
    total_usage_.completion_tokens += usage.completion_tokens;
    total_usage_.total_tokens += usage.total_tokens;
}
