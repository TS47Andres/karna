#include <catch2/catch_test_macros.hpp>
#include "process/runner.h"

#include <catch2/matchers/catch_matchers_string.hpp>

TEST_CASE("ProcessRunner executes command", "[process]")
{
#ifdef _WIN32
    auto result = ProcessRunner::run("echo hello", "", 5000);
#else
    auto result = ProcessRunner::run("echo hello", "", 5000);
#endif
    REQUIRE(result.exit_code == 0);
    REQUIRE_THAT(result.stdout_str, Catch::Matchers::ContainsSubstring("hello"));
}

TEST_CASE("ProcessRunner timeout", "[process]")
{
#ifdef _WIN32
    auto result = ProcessRunner::run("ping -n 10 127.0.0.1", "", 100);
#else
    auto result = ProcessRunner::run("sleep 10", "", 100);
#endif
    REQUIRE(result.timed_out);
}
