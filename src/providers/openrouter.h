#pragma once

#include "core/provider.h"
#include "config/config.h"
#include <thread>
#include <mutex>

class OpenRouterProvider : public Provider {
public:
    explicit OpenRouterProvider(const ProviderConfig& config);
    ~OpenRouterProvider() override;

    std::string name() const override;
    void send(
        const std::vector<Message>& history,
        const json& tools,
        const std::string& model,
        std::function<void(Delta)> on_delta,
        std::function<void(std::string)> on_error,
        std::function<void(Usage)> on_done
    ) override;
    void abort() override;
    void set_model(const std::string& model) override;
    void set_api_key(const std::string& api_key) override;
    std::vector<ModelInfo> available_models() const override;
    int context_window() const override;
    int count_tokens(const std::string& text) const override;
    std::string model() const override;

    void set_temperature(double temp);
    void set_max_tokens(int max);

private:
    ProviderConfig config_;
    std::string model_;
    double temperature_;
    int max_tokens_;
    std::atomic<bool> abort_{false};
    mutable std::mutex worker_mutex_;
    std::thread worker_;
    int context_window_{0};
    mutable std::mutex metadata_mutex_;
    mutable std::vector<ModelInfo> model_catalog_;

    struct WriteCtx;
    static size_t write_callback(char* data, size_t size, size_t nmemb, void* userp);

    json build_request_body(
        const std::vector<Message>& history,
        const json& tools,
        const std::string& model
    ) const;

    std::string role_to_string(MessageRole role) const;
    std::vector<ModelInfo> fetch_models() const;
};
