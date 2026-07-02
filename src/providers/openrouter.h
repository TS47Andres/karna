#pragma once

#include "core/provider.h"
#include "config/config.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class OpenRouterProvider : public Provider {
public:
    explicit OpenRouterProvider(const ProviderConfig& config);

    std::string name() const override;
    void send(
        const std::vector<Message>& history,
        const std::vector<std::string>& tool_names,
        std::function<void(Delta)> on_delta,
        std::function<void(std::string)> on_error,
        std::function<void(Usage)> on_done
    ) override;
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

    struct WriteCtx;
    static size_t write_callback(char* data, size_t size, size_t nmemb, void* userp);

    json build_request_body(
        const std::vector<Message>& history,
        const std::vector<std::string>& tool_names
    ) const;

    std::string role_to_string(MessageRole role) const;
    void process_sse_line(const std::string& line, std::string& buffer, std::function<void(Delta)>& on_delta, std::function<void(std::string)>& on_error, std::function<void(Usage)>& on_done);
};
