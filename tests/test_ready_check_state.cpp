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
    state.confirm(300, 2);
    CHECK(state.status(300) == "notready"sv);
    CHECK(state.count(true) == 0);
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


#include "game/ready_check_completion.hpp"
#include <stdexcept>

TEST_CASE("only originating client finishes after every real answer", "[ready-check]") {
    wowee::game::ReadyCheckCompletion completion;
    ReadyCheckState state;
    int sends = 0;
    const auto send = [&](const wowee::network::Packet& packet) {
        CHECK(packet.getOpcode() == 0x3c6);
        CHECK(packet.getData().empty());
        ++sends;
    };
    state.start();
    completion.start(100, 100, {200});
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    completion.confirm(999); // A foreign GUID cannot replace an expected answer.
    completion.confirm(200);
    state.confirm(200, 0);
    completion.confirm(200);
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    completion.confirm(100);
    state.confirm(100, 1);
    REQUIRE(completion.sendIfComplete(0x3c6, send));
    CHECK(sends == 1);
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    CHECK(state.status(300) == "waiting"sv); // Sending is not server FINISHED.
    state.finish();
    completion.reset();
    CHECK(state.status(200) == "notready"sv);
    CHECK(state.status(300) == nullptr);
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
}

TEST_CASE("ready check completion invalidates foreign and stale ownership", "[ready-check]") {
    wowee::game::ReadyCheckCompletion completion;
    int sends = 0;
    auto send = [&](const wowee::network::Packet&) { ++sends; };
    completion.start(200, 100, {100});
    completion.confirm(100);
    completion.confirm(200);
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    completion.start(100, 100, {0, 100, 200, 200});
    completion.confirm(100);
    completion.confirm(200);
    completion.validateRoster(100, {200, 300}, true);
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    completion.start(100, 100, {200});
    completion.confirm(100);
    completion.confirm(200);
    completion.validateRoster(100, {200}, false); // Lost leader/assistant role.
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    completion.start(100, 100, {200});
    completion.confirm(100);
    completion.confirm(200);
    completion.reset(); // Group exit or disconnect.
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    completion.start(100, 100, {200});
    completion.confirm(100);
    completion.confirm(200);
    completion.start(100, 100, {200}); // New authoritative start clears answers.
    CHECK_FALSE(completion.sendIfComplete(0x3c6, send));
    CHECK(sends == 0);
}

TEST_CASE("ready check finish request records only a returned send", "[ready-check]") {
    wowee::game::ReadyCheckCompletion completion;
    completion.start(100, 100, {200});
    completion.confirm(100);
    completion.confirm(200);
    REQUIRE_THROWS_AS(completion.sendIfComplete(0x3c6,
        [](const wowee::network::Packet&) { throw std::runtime_error("send failed"); }),
        std::runtime_error);
    int sends = 0;
    CHECK_FALSE(completion.sendIfComplete(0xFFFF, [](const wowee::network::Packet&) { FAIL("unsupported opcode sent"); }));
    completion.validateRoster(100, {200}, true);
    CHECK(completion.sendIfComplete(0x3c6, [&](const wowee::network::Packet&) { ++sends; }));
    CHECK(sends == 1);
}
