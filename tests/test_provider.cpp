#include <catch2/catch_test_macros.hpp>

#include "providers/openrouter.h"

#include <chrono>
#include <future>

TEST_CASE("OpenRouter continuation survives arbitrary tool bytes", "[provider][tools]")
{
    ProviderConfig config;
    config.api_key = "test-key";
    config.base_url = "http://127.0.0.1:1";
    config.default_model = "test/model";

    OpenRouterProvider provider(config);
    std::vector<Message> history;

    Message tool_result;
    tool_result.role = MessageRole::Tool;
    tool_result.tool_call_id = "call_1";
    tool_result.name = "read";
    tool_result.content = std::string("arbitrary tool bytes: ") +
        static_cast<char>(0xff) + static_cast<char>(0xfe);
    history.push_back(std::move(tool_result));

    // The second request joins and replaces the first request's completed
    // worker, matching the provider continuation after a tool result.
    for (int request = 0; request < 2; ++request) {
        std::promise<void> completed;
        auto future = completed.get_future();
        provider.send(
            history,
            json::array(),
            "test/model",
            [](Delta) {},
            [&completed](std::string) {
                try { completed.set_value(); } catch (...) {}
            },
            [&completed](Usage) {
                try { completed.set_value(); } catch (...) {}
            });

        REQUIRE(future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    }
}
