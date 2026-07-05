#include <catch2/catch_test_macros.hpp>
#include "config/config.h"
#include <filesystem>

TEST_CASE("Config loads defaults", "[config]")
{
    Config cfg = Config::load_from_file("nonexistent.toml");
    REQUIRE(cfg.openrouter.base_url == "https://openrouter.ai/api/v1");
    REQUIRE(cfg.openrouter.default_model == "deepseek/deepseek-v4-flash");
    REQUIRE(cfg.openrouter.max_tokens == 4096);
    REQUIRE(cfg.exa.num_results == 5);
    REQUIRE(cfg.theme == "default");
    REQUIRE_FALSE(cfg.auto_approve_commands);
}

TEST_CASE("Config roundtrip", "[config]")
{
    Config cfg;
    cfg.openrouter.api_key = "test-key";
    cfg.openrouter.default_model = "anthropic/claude-sonnet-4";
    cfg.exa.api_key = "exa-test";
    cfg.theme = "dark";

    cfg.save("test_config_output.toml");

    Config loaded = Config::load_from_file("test_config_output.toml");
    REQUIRE(loaded.openrouter.api_key == "test-key");
    REQUIRE(loaded.openrouter.default_model == "anthropic/claude-sonnet-4");
    REQUIRE(loaded.exa.api_key == "exa-test");
    REQUIRE(loaded.theme == "dark");

    std::filesystem::remove("test_config_output.toml");
}
