#include "core/session.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {
std::string title_from_first_message(const std::string& content)
{
    std::string title;
    title.reserve(20);
    for (const char c : content) {
        if (title.size() >= 20) break;
        title += (c == '\r' || c == '\n') ? ' ' : c;
    }
    return title.empty() ? "New session" : title;
}

std::string now_string()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
}

Session::Session(const Config& config, std::string id, std::string title)
    : model_(config.openrouter.default_model)
    , id_(std::move(id))
    , title_(std::move(title))
    , created_at_(now_string())
    , updated_at_(created_at_)
{
}

void Session::add_message(const Message& msg)
{
    if (msg.role == MessageRole::User && title_ == "New session") {
        title_ = title_from_first_message(msg.content);
    }
    history_.push_back(msg);
    updated_at_ = now_string();
}

void Session::clear()
{
    history_.clear();
    context_usage_ = 0;
    total_usage_ = {};
    updated_at_ = now_string();
}

void Session::set_model(const std::string& model)
{
    const auto first = model.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return;
    }
    const auto last = model.find_last_not_of(" \t\r\n");
    model_ = model.substr(first, last - first + 1);
    updated_at_ = now_string();
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
    total_usage_.cost += usage.cost;
    updated_at_ = now_string();
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

const std::string& Session::id() const
{
    return id_;
}

const std::string& Session::title() const
{
    return title_;
}

const std::string& Session::updated_at() const
{
    return updated_at_;
}

SessionData Session::snapshot() const
{
    return {
        id_, title_, created_at_, updated_at_, model_, history_,
        total_usage_, context_usage_
    };
}

void Session::restore(const SessionData& data)
{
    if (!data.id.empty()) id_ = data.id;
    if (!data.title.empty()) title_ = data.title;
    if (!data.created_at.empty()) created_at_ = data.created_at;
    if (!data.updated_at.empty()) updated_at_ = data.updated_at;
    if (!data.model.empty()) model_ = data.model;
    history_ = data.history;
    total_usage_ = data.usage;
    context_usage_ = std::max(0, data.context_usage);
    if (title_.empty()) {
        for (const auto& message : history_) {
            if (message.role == MessageRole::User) {
                title_ = title_from_first_message(message.content);
                break;
            }
        }
    }
}
