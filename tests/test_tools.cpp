#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "tools/read.h"
#include "tools/write.h"
#include "tools/edit.h"
#include "tools/bash.h"
#include "tools/sub_agent.h"
#include "tools/glob.h"
#include "tools/grep.h"

#include <filesystem>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

TEST_CASE("ReadTool handles nonexistent file", "[tools]")
{
    ReadTool tool;
    auto result = tool.execute(json::parse(R"({"path": "/nonexistent/file.txt"})"));
    REQUIRE_FALSE(result.success);
    REQUIRE_THAT(result.output, Catch::Matchers::ContainsSubstring("not found"));
}

TEST_CASE("WriteTool creates and writes file", "[tools]")
{
    fs::create_directories("test_tmp");

    WriteTool tool;
    auto result = tool.execute(json::parse(R"({"path": "test_tmp/write_test.txt", "content": "hello world"})"));
    REQUIRE(result.success);
    REQUIRE(result.data["before"] == "");
    REQUIRE(result.data["after"] == "hello world");

    {
        std::ifstream f("test_tmp/write_test.txt");
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        REQUIRE(content == "hello world");
    }

    fs::remove_all("test_tmp");
}

TEST_CASE("EditTool replaces text", "[tools]")
{
    fs::create_directories("test_tmp");
    {
        std::ofstream f("test_tmp/edit_test.txt");
        f << "old content";
    }

    EditTool tool;
    auto result = tool.execute(json::parse(R"({"path": "test_tmp/edit_test.txt", "old_string": "old", "new_string": "new"})"));
    REQUIRE(result.success);
    REQUIRE(result.data["before"] == "old content");
    REQUIRE(result.data["after"] == "new content");

    {
        std::ifstream f("test_tmp/edit_test.txt");
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        REQUIRE(content == "new content");
    }

    fs::remove_all("test_tmp");
}

TEST_CASE("BashTool streams output and reports timeout metadata", "[tools]")
{
    BashTool tool;
    std::vector<std::string> chunks;
    auto result = tool.execute_stream(
        json::parse(R"({"command": "echo streamed"})"),
        [&chunks](const std::string& chunk) { chunks.push_back(chunk); });

    REQUIRE(tool.name() == "bash");
    REQUIRE(result.success);
    REQUIRE(result.data["timeout_set"] == false);
    REQUIRE_FALSE(chunks.empty());
    REQUIRE_THAT(result.output, Catch::Matchers::ContainsSubstring("streamed"));
}

TEST_CASE("BashTool reports nonzero exit codes as failures", "[tools]")
{
    BashTool tool;
    auto result = tool.execute_stream(
        json::parse(R"({"command": "exit 7"})"), {});

    REQUIRE_FALSE(result.success);
    REQUIRE(result.data["exit_code"] == 7);
    REQUIRE_THAT(result.output, Catch::Matchers::ContainsSubstring("Exit code: 7"));
}

TEST_CASE("SubAgentTool exposes read and read-write modes", "[tools]")
{
    SubAgentTool tool(
        [] { return Config{}; },
        [] { return std::string("test-model"); });
    const auto schema = tool.parameters();

    REQUIRE(tool.name() == "sub_agent");
    REQUIRE(schema["properties"]["mode"]["enum"] == json({"R", "RW"}));
    REQUIRE(schema["properties"]["mode"]["default"] == "R");
}
