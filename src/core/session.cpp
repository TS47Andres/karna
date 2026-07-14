#include "core/session.h"
#include <algorithm>

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
    context_usage_ = 0;
}

void Session::set_model(const std::string& model)
{
    const auto first = model.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return;
    }
    const auto last = model.find_last_not_of(" \t\r\n");
    model_ = model.substr(first, last - first + 1);
    if (provider_) {
        provider_->set_model(model_);
        context_usage_ = estimate_context_usage(*provider_);
    } else {
        context_usage_ = 0;
    }
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

int Session::context_usage() const
{
    return context_usage_;
}

void Session::set_context_usage(int tokens)
{
    context_usage_ = std::max(0, tokens);
}

int Session::estimate_context_usage(const Provider& provider) const
{
    int tokens = 0;
    for (const auto& message : history_) {
        tokens += provider.count_tokens(message.content);
        for (const auto& tool_call : message.tool_calls) {
            tokens += provider.count_tokens(tool_call.function_name + tool_call.arguments);
        }
    }
    return tokens;
}
