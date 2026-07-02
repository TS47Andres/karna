#pragma once

#include "core/provider.h"
#include "config/config.h"

class OpenRouterProvider : public Provider {
public:
    explicit OpenRouterProvider(const ProviderConfig& config);
    ~OpenRouterProvider() override;

    std::string name() const override;
    void send(
        const std::vector<Message>& history,
        const json& tools,
        std::function<void(Delta)> on_delta,
        std::function<void(std::string)> on_error,
        std::function<void(Usage)> on_done
    ) override;
    void abort() override;
    int count_tokens(const std::string& text) const override;
    std::string model() const override;

    void set_model(const std::string& model);
    void set_temperature(double temp);
    void set_max_tokens(int max);

private:
    ProviderConfig config_;
    std::string model_;
    double temperature_;
    int max_tokens_;
    std::atomic<bool> abort_{false};

    struct WriteCtx;
    static size_t write_callback(char* data, size_t size, size_t nmemb, void* userp);

    json build_request_body(
        const std::vector<Message>& history,
        const json& tools
    ) const;

    std::string role_to_string(MessageRole role) const;
};
