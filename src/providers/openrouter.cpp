#include "providers/openrouter.h"
#include "core/system_prompt.h"
#include "token/counter.h"
#include "project/context.h"

#include <curl/curl.h>
#include <thread>
#include <sstream>
#include <iostream>
#include <map>
#include <filesystem>

struct OpenRouterProvider::WriteCtx {
    std::string buffer;
    std::atomic<bool>* abort_flag;
    std::function<void(Delta)> const* on_delta;
    std::function<void(std::string)> const* on_error;
    Usage final_usage;
    std::map<int, ToolCall> accumulated_calls;
};

OpenRouterProvider::OpenRouterProvider(const ProviderConfig& config)
    : config_(config)
    , model_(config.default_model)
    , temperature_(config.temperature)
    , max_tokens_(config.max_tokens)
{}

OpenRouterProvider::~OpenRouterProvider()
{
    abort();
}

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

void OpenRouterProvider::abort()
{
    abort_.store(true);
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
    const json& tools
) const
{
    json body;
    body["model"] = model_;
    body["stream"] = true;
    body["max_tokens"] = max_tokens_;
    body["temperature"] = temperature_;

    json messages = json::array();

    std::string sys_prompt = karna::SYSTEM_PROMPT;
    auto proj_ctx = ProjectContext::discover();
    sys_prompt += "\n\n# Active Project Context\n";
    sys_prompt += "- Root Path: " + std::filesystem::current_path().string() + "\n";
    if (proj_ctx.has_git) {
        sys_prompt += "- Git Branch: " + proj_ctx.git_branch + "\n";
    }

    json sys_msg;
    sys_msg["role"] = "system";
    sys_msg["content"] = sys_prompt;
    messages.push_back(sys_msg);

    for (const auto& msg : history) {
        json j;
        j["role"] = role_to_string(msg.role);

        if (msg.role == MessageRole::Tool) {
            j["content"] = msg.content;
            if (msg.tool_call_id) {
                j["tool_call_id"] = *msg.tool_call_id;
            }
        } else if (msg.role == MessageRole::Assistant && !msg.tool_calls.empty()) {
            j["content"] = msg.content.empty() ? nullptr : msg.content;
            json tc_array = json::array();
            for (const auto& tc : msg.tool_calls) {
                json tcj;
                tcj["id"] = tc.id;
                tcj["type"] = tc.type;
                tcj["function"] = json::object();
                tcj["function"]["name"] = tc.function_name;
                tcj["function"]["arguments"] = tc.arguments;
                tc_array.push_back(tcj);
            }
            j["tool_calls"] = tc_array;
        } else {
            j["content"] = msg.content;
        }

        if (msg.name) {
            j["name"] = *msg.name;
        }

        messages.push_back(j);
    }
    body["messages"] = messages;

    if (!tools.empty()) {
        body["tools"] = tools;
    }

    return body;
}

size_t OpenRouterProvider::write_callback(char* data, size_t size, size_t nmemb, void* userp)
{
    size_t total = size * nmemb;
    auto* ctx = static_cast<WriteCtx*>(userp);

    if (ctx->abort_flag && ctx->abort_flag->load()) {
        return 0;
    }

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
                    ctx->final_usage.prompt_tokens = j["usage"].value("prompt_tokens", 0);
                    ctx->final_usage.completion_tokens = j["usage"].value("completion_tokens", 0);
                    ctx->final_usage.total_tokens = j["usage"].value("total_tokens", 0);
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
                                int idx = tc.value("index", 0);
                                auto& acc = ctx->accumulated_calls[idx];

                                if (tc.contains("id") && !tc["id"].is_null()) {
                                    acc.id = tc["id"].get<std::string>();
                                }
                                if (tc.contains("type") && !tc["type"].is_null()) {
                                    acc.type = tc["type"].get<std::string>();
                                }
                                if (tc.contains("function")) {
                                    const auto& fn = tc["function"];
                                    if (fn.contains("name") && !fn["name"].is_null()) {
                                        acc.function_name = fn["name"].get<std::string>();
                                    }
                                    if (fn.contains("arguments") && !fn["arguments"].is_null()) {
                                        acc.arguments += fn["arguments"].get<std::string>();
                                    }
                                }
                                acc.index = idx;

                                if (!acc.function_name.empty() || (tc.contains("function") && tc["function"].contains("name"))) {
                                    ToolCall out = acc;
                                    delta.tool_call = out;
                                    has_content = true;
                                }
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
    const json& tools,
    std::function<void(Delta)> on_delta,
    std::function<void(std::string)> on_error,
    std::function<void(Usage)> on_done)
{
    abort_.store(false);

    std::thread([this, history, tools, on_delta = std::move(on_delta), on_error = std::move(on_error), on_done = std::move(on_done)]() {
        CURL* curl = curl_easy_init();
        if (!curl) {
            if (on_error) on_error("Failed to initialize curl");
            return;
        }

        json body = build_request_body(history, tools);
        std::string body_str = body.dump();

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth = "Authorization: Bearer " + config_.api_key;
        headers = curl_slist_append(headers, auth.c_str());
        headers = curl_slist_append(headers, "Accept: text/event-stream");

        WriteCtx ctx;
        ctx.abort_flag = &abort_;
        ctx.on_delta = &on_delta;
        ctx.on_error = &on_error;

        std::string url = config_.base_url + "/chat/completions";
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
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
            std::string err = curl_easy_strerror(res);
            if (abort_.load()) {
                err = "Request aborted";
            }
            if (on_error) on_error(std::string("Request failed: ") + err);
        } else if (on_done) {
            on_done(ctx.final_usage);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }).detach();
}

int OpenRouterProvider::count_tokens(const std::string& text) const
{
    return TokenCounter::estimate_for_model(text, model_);
}
