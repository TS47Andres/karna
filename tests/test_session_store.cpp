#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "core/session.h"
#include "core/session_store.h"

TEST_CASE("Session store roundtrips a conversation", "[sessions]")
{
    Config config;
    Session session(config, SessionStore::new_id(), "Store test");

    Message message;
    message.role = MessageRole::User;
    message.content = "persist me";
    session.add_message(message);
    Usage usage;
    usage.prompt_tokens = 12;
    usage.completion_tokens = 8;
    usage.total_tokens = 20;
    usage.cost = 0.001234;
    session.add_usage(usage);

    SessionStore store;
    REQUIRE(store.save(session.snapshot()));

    const auto loaded = store.load(session.id());
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->title == "Store test");
    REQUIRE(loaded->history.size() == 1);
    REQUIRE(loaded->history.front().content == "persist me");
    REQUIRE(loaded->usage.prompt_tokens == 12);
    REQUIRE(loaded->usage.completion_tokens == 8);
    REQUIRE(loaded->usage.total_tokens == 20);
    REQUIRE(std::abs(loaded->usage.cost - 0.001234) < 1e-9);

    REQUIRE(store.remove(session.id()));
    REQUIRE_FALSE(store.load(session.id()).has_value());
}
