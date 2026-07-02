#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "tools/read.h"
#include "tools/write.h"
#include "tools/edit.h"
#include "tools/glob.h"
#include "tools/grep.h"

#include <filesystem>
#include <fstream>

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

    std::ifstream f("test_tmp/write_test.txt");
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(content == "hello world");

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

    std::ifstream f("test_tmp/edit_test.txt");
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(content == "new content");

    fs::remove_all("test_tmp");
}
