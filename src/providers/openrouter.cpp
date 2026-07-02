#include "providers/openrouter.h"
#include "token/counter.h"

#include <curl/curl.h>
#include <thread>
#include <sstream>
#include <iostream>

struct OpenRouterProvider::WriteCtx {
    std::string buffer;
    std::function<void(Delta)> const* on_delta;
    std::function<void(std::string)> const* on_error;
    std::function<void(Usage)> const* on_done;
};

OpenRouterProvider::OpenRouterProvider(const ProviderConfig& config)
    : config_(config)
    , model_(config.default_model)
    , temperature_(config.temperature)
    , max_tokens_(config.max_tokens)
{}

std::string OpenRouterProvider::name() const
{
    return "openrouter";
}

std::string OpenRouterProvider::model() const
{
    return model_;
}

void OpenRouterProvider::set_model(const std::string& model)
{
    model_ = model;
}

void OpenRouterProvider::set_temperature(double temp)
{
    temperature_ = temp;
}

void OpenRouterProvider::set_max_tokens(int max)
{
    max_tokens_ = max;
}

std::string OpenRouterProvider::role_to_string(MessageRole role) const
{
    switch (role) {
        case MessageRole::System: return "system";
        case MessageRole::User: return "user";
        case MessageRole::Assistant: return "assistant";
        case MessageRole::Tool: return "tool";
    }
    return "user";
}

json OpenRouterProvider::build_request_body(
    const std::vector<Message>& history,
    const std::vector<std::string>& tool_names
) const
{
    json body;
    body["model"] = model_;
    body["stream"] = true;
    body["max_tokens"] = max_tokens_;
    body["temperature"] = temperature_;

    json messages = json::array();
    for (const auto& msg : history) {
        json j;
        j["role"] = role_to_string(msg.role);

        if (msg.role == MessageRole::Tool) {
            j["content"] = msg.content;
            if (msg.tool_call_id) {
                j["tool_call_id"] = *msg.tool_call_id;
            }
        } else if (msg.role == MessageRole::Assistant && msg.tool_call) {
            j["content"] = msg.content.empty() ? nullptr : msg.content;
            json tool_calls = json::array();
            json tc;
            tc["id"] = msg.tool_call->id;
            tc["type"] = msg.tool_call->type;
            tc["function"] = json::object();
            tc["function"]["name"] = msg.tool_call->function_name;
            tc["function"]["arguments"] = msg.tool_call->arguments;
            tool_calls.push_back(tc);
            j["tool_calls"] = tool_calls;
        } else {
            j["content"] = msg.content;
        }

        if (msg.name) {
            j["name"] = *msg.name;
        }

        messages.push_back(j);
    }
    body["messages"] = messages;

    auto& reg = const_cast<OpenRouterProvider*>(this)->config_; // hack to get tools
    // tools will be resolved externally — for now, left empty
    if (!tool_names.empty()) {
        body["tools"] = json::array();
        // Each tool schema must be provided by the caller or looked up from ToolRegistry
        // We leave tool definitions to be injected by the controller
    }

    return body;
}

size_t OpenRouterProvider::write_callback(char* data, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    auto* ctx = static_cast<WriteCtx*>(userp);
    ctx->buffer.append(data, total);

    size_t pos = 0;
    while (true) {
        size_t newline = ctx->buffer.find('\n', pos);
        if (newline == std::string::npos) {
            break;
        }
        std::string line = ctx->buffer.substr(pos, newline - pos);
        pos = newline + 1;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        // Parse SSE data line
        if (line.rfind("data: ", 0) == 0) {
            std::string payload = line.substr(6);
            if (payload == "[DONE]") {
                break;
            }

            try {
                auto j = json::parse(payload);
                if (j.contains("error")) {
                    std::string err = j["error"].value("message", "unknown error");
                    if (ctx->on_error && *ctx->on_error) {
                        (*ctx->on_error)(err);
                    }
                    continue;
                }

                Delta delta;
                bool has_content = false;

                if (j.contains("usage") && !j["usage"].is_null()) {
                    Usage usage;
                    usage.prompt_tokens = j["usage"].value("prompt_tokens", 0);
                    usage.completion_tokens = j["usage"].value("completion_tokens", 0);
                    usage.total_tokens = j["usage"].value("total_tokens", 0);
                    if (ctx->on_done && *ctx->on_done) {
                        (*ctx->on_done)(usage);
                    }
                }

                if (j.contains("choices") && !j["choices"].empty()) {
                    const auto& choice = j["choices"][0];

                    if (choice.contains("finish_reason") && !choice["finish_reason"].is_null()) {
                        std::string fr = choice["finish_reason"];
                        if (fr == "stop") {
                            delta.finish_reason = FinishReason::Stop;
                        } else if (fr == "tool_calls") {
                            delta.finish_reason = FinishReason::ToolCalls;
                        } else if (fr == "length") {
                            delta.finish_reason = FinishReason::Length;
                        } else {
                            delta.finish_reason = FinishReason::Unknown;
                        }
                        has_content = true;
                    }

                    if (choice.contains("delta")) {
                        const auto& d = choice["delta"];

                        if (d.contains("content") && !d["content"].is_null()) {
                            delta.content = d["content"].get<std::string>();
                            has_content = true;
                        }

                        if (d.contains("tool_calls") && !d["tool_calls"].empty()) {
                            const auto& tcs = d["tool_calls"];
                            for (const auto& tc : tcs) {
                                ToolCall tool_call;
                                tool_call.id = tc.value("id", "");
                                tool_call.type = tc.value("type", "function");
                                if (tc.contains("function")) {
                                    tool_call.function_name = tc["function"].value("name", "");
                                    tool_call.arguments = tc["function"].value("arguments", "");
                                }
                                delta.tool_call = tool_call;
                                has_content = true;
                            }
                        }
                    }
                }

                if (has_content && ctx->on_delta && *ctx->on_delta) {
                    (*ctx->on_delta)(delta);
                }
            } catch (const std::exception& e) {
                if (ctx->on_error && *ctx->on_error) {
                    (*ctx->on_error)(std::string("SSE parse error: ") + e.what());
                }
            }
        }
    }

    ctx->buffer.erase(0, pos);
    return total;
}

void OpenRouterProvider::send(
    const std::vector<Message>& history,
    const std::vector<std::string>& tool_names,
    std::function<void(Delta)> on_delta,
    std::function<void(std::string)> on_error,
    std::function<void(Usage)> on_done)
{
    std::thread([this, history, tool_names, on_delta = std::move(on_delta), on_error = std::move(on_error), on_done = std::move(on_done)]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (on_error) on_error("Failed to initialize curl");
            return;
        }

        json body = build_request_body(history, tool_names);
        std::string body_str = body.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth.c_str());
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        WriteCtx ctx;
        ctx.on_delta = &on_delta;
        ctx.on_error = &on_error;
        ctx.on_done = &on_done;

        curl_easy_setopt(curl, CURLOPT_URL, (config_.base_url + "/chat/completions").c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "karna/0.1.0");

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            if (on_error) on_error(std::string("Request failed: ") + curl_easy_strerror(res));
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }).detach();
}

int OpenRouterProvider::count_tokens(const std::string& text) const
{
    return TokenCounter::estimate_for_model(text, model_);
}
