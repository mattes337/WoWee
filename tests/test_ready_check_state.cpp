#include <catch_amalgamated.hpp>
#include "game/ready_check_state.hpp"
#include <string_view>

using wowee::game::ReadyCheckState;
using namespace std::literals;

TEST_CASE("ready check queries are keyed by GUID and retain answers through finish", "[ready-check]") {
    ReadyCheckState state;
    CHECK(state.status(100) == nullptr);
    state.start();
    CHECK(state.status(0) == nullptr);
    REQUIRE(state.status(100) != nullptr);
    CHECK(state.status(100) == "waiting"sv);
    state.confirm(100, true);
    state.confirm(200, false);
    CHECK(state.status(100) == "ready"sv);
    CHECK(state.status(200) == "notready"sv);
    CHECK(state.status(300) == "waiting"sv);
    state.finish();
    CHECK(state.status(100) == "ready"sv);
    CHECK(state.status(200) == "notready"sv);
    CHECK(state.status(300) == nullptr);
    state.start();
    CHECK(state.status(100) == "waiting"sv);
    CHECK(state.status(200) == "waiting"sv);
    CHECK(state.count(true) == 0);
    CHECK(state.count(false) == 0);
}

TEST_CASE("repeated ready check confirmations replace an answer without inflating counts", "[ready-check]") {
    ReadyCheckState state;
    state.start();
    state.confirm(100, true);
    state.confirm(100, true);
    state.confirm(200, false);
    CHECK(state.count(true) == 1);
    CHECK(state.count(false) == 1);
    state.confirm(100, false);
    CHECK(state.status(100) == "notready"sv);
    CHECK(state.count(true) == 0);
    CHECK(state.count(false) == 2);
    state.confirm(0, true);
    CHECK(state.count(true) == 0);
    CHECK(state.status(0) == nullptr);
}

TEST_CASE("leaving a group clears retained ready check answers", "[ready-check]") {
    ReadyCheckState state;
    state.start();
    state.confirm(100, true);
    state.finish();
    state.reset();
    CHECK(state.status(100) == nullptr);
    CHECK(state.status(200) == nullptr);
    CHECK(state.count(true) == 0);
    state.start();
    state.reset();
    CHECK(state.status(200) == nullptr);
}
