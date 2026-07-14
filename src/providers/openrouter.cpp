#include "providers/openrouter.h"
#include "net/curl_setup.h"
#include "core/system_prompt.h"
#include "token/counter.h"
#include "project/context.h"

#include <curl/curl.h>
#include <thread>
#include <sstream>
#include <iostream>
#include <map>
#include <filesystem>
#include <memory>
#include <stdexcept>

namespace {

using DeltaCallback = std::function<void(Delta)>;
using ErrorCallback = std::function<void(std::string)>;
using DoneCallback = std::function<void(Usage)>;

void notify_delta(const DeltaCallback& callback, const Delta& delta) noexcept
{
    try {
        if (callback) {
            callback(delta);
        }
    } catch (...) {
        // No C++ exception may cross libcurl's C callback boundary.
    }
}

void notify_error(
    const ErrorCallback& callback,
    const char* prefix,
    const char* detail = nullptr
) noexcept
{
    try {
        if (!callback) {
            return;
        }
        std::string message = prefix ? prefix : "Provider request failed";
        if (detail && *detail) {
            message += detail;
        }
        callback(std::move(message));
    } catch (...) {
        // Error reporting itself must never terminate a worker thread.
    }
}

void notify_done(const DoneCallback& callback, const Usage& usage) noexcept
{
    try {
        if (callback) {
            callback(usage);
        }
    } catch (...) {
        // The UI owns callback errors; never let one escape the worker.
    }
}

struct CurlHandleDeleter {
    void operator()(CURL* handle) const noexcept
    {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
};

class CurlHeaders {
public:
    ~CurlHeaders()
    {
        if (headers_) {
            curl_slist_free_all(headers_);
        }
    }

    void append(const std::string& value)
    {
        auto* updated = curl_slist_append(headers_, value.c_str());
        if (!updated) {
            throw std::runtime_error("Failed to allocate HTTP headers");
        }
        headers_ = updated;
    }

    curl_slist* get() const noexcept { return headers_; }

private:
    curl_slist* headers_{nullptr};
};

} // namespace

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
    std::lock_guard<std::mutex> lock(worker_mutex_);
    if (worker_.joinable()) {
        worker_.join();
    }
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
    auto models = fetch_models();
    int context_window = 0;
    for (const auto& model_info : models) {
        if (model_info.id == model_) {
            context_window = model_info.context_length;
        }
    }
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        model_catalog_ = std::move(models);
        context_window_ = context_window;
    }
}

void OpenRouterProvider::set_api_key(const std::string& api_key)
{
    config_.api_key = api_key;
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    model_catalog_.clear();
    context_window_ = 0;
}

std::vector<ModelInfo> OpenRouterProvider::available_models() const
{
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        if (!model_catalog_.empty()) {
            return model_catalog_;
        }
    }

    auto models = fetch_models();
    {
        std::lock_guard<std::mutex> lock(metadata_mutex_);
        model_catalog_ = models;
    }
    return models;
}

int OpenRouterProvider::context_window() const
{
    std::lock_guard<std::mutex> lock(metadata_mutex_);
    return context_window_;
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
    const json& tools,
    const std::string& model
) const
{
    json body;
    body["model"] = model;
    body["stream"] = true;
    body["max_tokens"] = max_tokens_;
    body["temperature"] = temperature_;
    body["stream_options"] = {{"include_usage", true}};

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

size_t OpenRouterProvider::write_callback(char* data, size_t size, size_t nmemb, void* userp) noexcept
{
    auto* ctx = static_cast<WriteCtx*>(userp);
    if (!ctx) {
        return 0;
    }

    try {
        const size_t total = size * nmemb;

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
                        if (ctx->on_error) {
                            notify_error(*ctx->on_error, "", err.c_str());
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

                    if (has_content && ctx->on_delta) {
                        notify_delta(*ctx->on_delta, delta);
                    }
                } catch (const std::exception& e) {
                    if (ctx->on_error) {
                        notify_error(*ctx->on_error, "SSE parse error: ", e.what());
                    }
                }
            }
        }

        ctx->buffer.erase(0, pos);
        return total;
    } catch (const std::exception& e) {
        if (ctx->on_error) {
            notify_error(*ctx->on_error, "Stream callback failed: ", e.what());
        }
    } catch (...) {
        if (ctx->on_error) {
            notify_error(*ctx->on_error, "Stream callback failed");
        }
    }
    return 0;
}

void OpenRouterProvider::send(
    const std::vector<Message>& history,
    const json& tools,
    const std::string& model,
    std::function<void(Delta)> on_delta,
    std::function<void(std::string)> on_error,
    std::function<void(Usage)> on_done)
{
    ErrorCallback start_error;
    try {
        start_error = on_error;
        abort_.store(false);

        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                notify_error(on_error, "Provider request could not be restarted from its own worker thread");
                return;
            }
            worker_.join();
        }

        auto task = [this, history, tools, request_model = model,
                     on_delta = std::move(on_delta), on_error = std::move(on_error),
                     on_done = std::move(on_done)]() noexcept {
            try {
                std::unique_ptr<CURL, CurlHandleDeleter> curl(curl_easy_init());
                if (!curl) {
                    notify_error(on_error, "Failed to initialize curl");
                    return;
                }
                configure_curl_ssl(curl.get());

                json body = build_request_body(history, tools, request_model);
                // Tool output can contain arbitrary bytes (terminal encodings,
                // binary files, and filenames). Replacing invalid UTF-8 keeps a
                // valid request and prevents nlohmann::json from throwing here.
                std::string body_str = body.dump(
                    -1, ' ', false, json::error_handler_t::replace);

                CurlHeaders headers;
                headers.append("Content-Type: application/json");
                headers.append("Authorization: Bearer " + config_.api_key);
                headers.append("Accept: text/event-stream");

                WriteCtx ctx;
                ctx.abort_flag = &abort_;
                ctx.on_delta = &on_delta;
                ctx.on_error = &on_error;

                std::string url = config_.base_url + "/chat/completions";
                curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
                curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body_str.c_str());
                curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
                curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &ctx);
                curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 300L);
                curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "karna/0.1.0");

                const CURLcode result = curl_easy_perform(curl.get());
                if (result != CURLE_OK) {
                    if (abort_.load()) {
                        notify_error(on_error, "Request aborted");
                    } else {
                        notify_error(on_error, "Request failed: ", curl_easy_strerror(result));
                    }
                } else {
                    notify_done(on_done, ctx.final_usage);
                }
            } catch (const std::exception& e) {
                notify_error(on_error, "Provider worker failed: ", e.what());
            } catch (...) {
                notify_error(on_error, "Provider worker failed with an unknown error");
            }
        };

        // worker_ is guaranteed non-joinable here. Constructing a temporary
        // first means a thread creation failure cannot assign over live state.
        std::thread next_worker(std::move(task));
        worker_ = std::move(next_worker);
    } catch (const std::exception& e) {
        notify_error(start_error, "Failed to start provider request: ", e.what());
    } catch (...) {
        notify_error(start_error, "Failed to start provider request");
    }
}

int OpenRouterProvider::count_tokens(const std::string& text) const
{
    return TokenCounter::estimate_for_model(text, model_);
}

std::vector<ModelInfo> OpenRouterProvider::fetch_models() const
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        return {};
    }
    configure_curl_ssl(curl);

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    std::string auth = "Authorization: Bearer " + config_.api_key;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, (config_.base_url + "/models").c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* data, size_t size, size_t nmemb, void* userp) {
        auto* output = static_cast<std::string*>(userp);
        output->append(data, size * nmemb);
        return size * nmemb;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

    CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (result != CURLE_OK || status < 200 || status >= 300) {
        return {};
    }

    std::vector<ModelInfo> models;
    try {
        auto payload = json::parse(response);
        for (const auto& model : payload.at("data")) {
            ModelInfo info;
            info.id = model.value("id", "");
            info.name = model.value("name", "");
            info.owned_by = model.value("owned_by", "");
            info.context_length = model.value("context_length", 0);
            if (info.context_length <= 0 && model.contains("top_provider") && model["top_provider"].is_object()) {
                info.context_length = model["top_provider"].value("context_length", 0);
            }
            if (!info.id.empty()) {
                models.push_back(std::move(info));
            }
        }
    } catch (...) {
        return {};
    }
    return models;
}
