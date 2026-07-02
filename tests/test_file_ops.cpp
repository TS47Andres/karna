#include <catch2/catch_test_macros.hpp>
#include "io/file_ops.h"

#include <filesystem>

TEST_CASE("FileOps read/write", "[io]")
{
    REQUIRE(FileOps::write_file("test_io.txt", "test content"));
    REQUIRE(FileOps::file_exists("test_io.txt"));

    auto content = FileOps::read_file("test_io.txt");
    REQUIRE(content.has_value());
    REQUIRE(*content == "test content");

    REQUIRE(FileOps::remove_file("test_io.txt"));
    REQUIRE_FALSE(FileOps::file_exists("test_io.txt"));
}

TEST_CASE("FileOps directory operations", "[io]")
{
    REQUIRE(FileOps::create_directory("test_dir/nested"));
    auto entries = FileOps::list_directory("test_dir");
    REQUIRE(entries.size() == 1);

    std::filesystem::remove_all("test_dir");
}
