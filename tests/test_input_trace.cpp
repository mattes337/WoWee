#include <catch_amalgamated.hpp>
#include "core/test_input_trace.hpp"
#include <stdexcept>

using namespace wowee::core;

namespace {
const char* sample = R"json({"version":1,"stop_after_updates":3,"events":[
 {"after_updates":0,"type":"mouse_move","x":20,"y":30},
 {"after_updates":0,"type":"mouse_down","x":20,"y":30,"button":1},
 {"after_updates":1,"type":"mouse_up","x":20,"y":30,"button":1},
 {"after_updates":1,"type":"mouse_wheel","x":0,"y":-1},
 {"after_updates":1,"type":"key_down","keycode":97,"scancode":4},
 {"after_updates":2,"type":"text","text":"hello"},
 {"after_updates":2,"type":"key_up","keycode":97,"scancode":4}
]})json";

std::string oneEvent(const std::string& event, int stop = 2) {
    return "{\"version\":1,\"stop_after_updates\":" + std::to_string(stop) + ",\"events\":[" + event + "]}";
}
}

TEST_CASE("input trace queues in declared order at exact completed updates", "[input-trace]") {
    auto trace = TestInputTrace::parse(sample);
    REQUIRE(trace.enabled());
    CHECK(trace.stopAfterUpdates() == 3);
    CHECK(trace.size() == 7);
    std::vector<TestInputEvent::Kind> received;
    auto push = [&](const TestInputEvent& event) { received.push_back(event.kind); return 1; };
    trace.queueDue(0, push);
    CHECK(received.size() == 2);
    trace.queueDue(0, push);
    CHECK(received.size() == 2);
    trace.queueDue(1, push);
    CHECK(received.size() == 5);
    trace.queueDue(2, push);
    CHECK(received.size() == 7);
    CHECK(received[0] == TestInputEvent::Kind::MouseMove);
    CHECK(received[5] == TestInputEvent::Kind::Text);
    CHECK(trace.events()[5].text == "hello");
    CHECK_THROWS(trace.requireComplete(2));
    CHECK_NOTHROW(trace.requireComplete(3));
}

TEST_CASE("invalid or missing input traces fail without echoing text", "[input-trace]") {
    CHECK_FALSE(TestInputTrace::fromFile(nullptr).enabled());
    CHECK_THROWS(TestInputTrace::fromFile(""));
    CHECK_THROWS(TestInputTrace::fromFile("missing-input-trace-never-created.json"));
    for (const auto* text : {"", "{}", "[]", "{broken", "{\"version\":1,\"stop_after_updates\":0,\"events\":[]}"}) {
        CHECK_THROWS_AS(TestInputTrace::parse(text), std::invalid_argument);
    }
    CHECK_THROWS(TestInputTrace::parse(oneEvent(R"({"after_updates":0,"type":"mouse_down","x":0,"y":0,"button":0})")));
    CHECK_THROWS(TestInputTrace::parse(oneEvent(R"({"after_updates":2,"type":"text","text":"a"})")));
    CHECK_THROWS(TestInputTrace::parse(oneEvent(R"({"after_updates":0,"type":"text","text":"a","typo":1})")));
    CHECK_THROWS(TestInputTrace::parse(oneEvent(R"({"after_updates":0.5,"type":"text","text":"a"})")));
    CHECK_THROWS(TestInputTrace::parse(oneEvent(R"({"after_updates":18446744073709551615,"type":"text","text":"a"})")));
    CHECK_THROWS(TestInputTrace::parse(oneEvent(R"({"after_updates":0,"type":"text","text":"\u0000"})")));
    CHECK_THROWS(TestInputTrace::parse(oneEvent("{\"after_updates\":0,\"type\":\"text\",\"text\":\"" + std::string(32, 'x') + "\"}")));
    const std::string privatePayload = "DO_NOT_LOG_PRIVATE_PAYLOAD";
    try {
        TestInputTrace::parse(oneEvent("{\"after_updates\":0,\"type\":\"" + privatePayload + "\"}"));
        FAIL("unknown type was accepted");
    } catch (const std::invalid_argument& error) {
        CHECK(std::string(error.what()).find(privatePayload) == std::string::npos);
    }
    try {
        TestInputTrace::parse("{\"text\":\"" + privatePayload);
        FAIL("broken JSON was accepted");
    } catch (const std::invalid_argument& error) {
        CHECK(std::string(error.what()).find(privatePayload) == std::string::npos);
    }
}

TEST_CASE("input queue rejection missed update and unfinished trace cannot succeed", "[input-trace]") {
    auto trace = TestInputTrace::parse(sample);
    CHECK_THROWS(trace.queueDue(0, [](const TestInputEvent&) { return 0; }));
    CHECK_THROWS(trace.queueDue(0, [](const TestInputEvent&) { return -1; }));
    CHECK_THROWS(trace.requireComplete(3));
    CHECK_THROWS(trace.queueDue(1, [](const TestInputEvent&) { return 1; }));
    CHECK_THROWS(TestInputTrace::parse(R"({"version":1,"stop_after_updates":3,"events":[
        {"after_updates":2,"type":"text","text":"a"},
        {"after_updates":1,"type":"text","text":"b"}]})"));
}
