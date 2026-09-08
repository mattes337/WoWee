#define SDL_MAIN_HANDLED
#include <catch_amalgamated.hpp>
#include "core/test_input_trace.hpp"
#include <SDL.h>
#include <string>

using namespace wowee::core;

namespace {
struct Events {
    Events() { REQUIRE(SDL_Init(SDL_INIT_EVENTS) == 0); SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); }
    ~Events() { SDL_SetEventFilter(nullptr, nullptr); SDL_Quit(); }
};
}

TEST_CASE("trace input traverses the real SDL event queue with complete fields", "[input-trace][sdl]") {
    Events events;
    auto trace = TestInputTrace::parse(R"({"version":1,"stop_after_updates":2,"events":[
        {"after_updates":0,"type":"mouse_move","x":20,"y":30,"dx":2,"dy":3,"buttons":1},
        {"after_updates":0,"type":"mouse_down","x":20,"y":30,"button":1},
        {"after_updates":0,"type":"mouse_up","x":20,"y":30,"button":1},
        {"after_updates":0,"type":"mouse_wheel","x":0,"y":-2},
        {"after_updates":0,"type":"key_down","keycode":97,"scancode":4,"modifiers":1},
        {"after_updates":0,"type":"text","text":"text"},
        {"after_updates":0,"type":"key_up","keycode":97,"scancode":4}
    ]})");
    trace.queueDue(0, [](const TestInputEvent& event) { return pushTestInputEvent(event, 7); });
    SDL_Event event{};
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_MOUSEMOTION);
    CHECK(event.motion.windowID == 7);
    CHECK(event.motion.x == 20);
    CHECK(event.motion.y == 30);
    CHECK(event.motion.xrel == 2);
    CHECK(event.motion.yrel == 3);
    CHECK(event.motion.state == SDL_BUTTON_LMASK);
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_MOUSEBUTTONDOWN);
    CHECK(event.button.state == SDL_PRESSED);
    CHECK(event.button.button == SDL_BUTTON_LEFT);
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_MOUSEBUTTONUP);
    CHECK(event.button.state == SDL_RELEASED);
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_MOUSEWHEEL);
    CHECK(event.wheel.y == -2);
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_KEYDOWN);
    CHECK(event.key.keysym.sym == SDLK_a);
    CHECK(event.key.keysym.scancode == SDL_SCANCODE_A);
    CHECK(event.key.keysym.mod == KMOD_LSHIFT);
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_TEXTINPUT);
    CHECK(std::string(event.text.text) == "text");
    REQUIRE(SDL_PollEvent(&event) == 1);
    CHECK(event.type == SDL_KEYUP);
    CHECK(SDL_PollEvent(&event) == 0);
}

TEST_CASE("SDL event filtering is a trace failure", "[input-trace][sdl]") {
    Events events;
    SDL_SetEventFilter([](void*, SDL_Event*) -> int { return 0; }, nullptr);
    auto trace = TestInputTrace::parse(R"({"version":1,"stop_after_updates":1,"events":[
        {"after_updates":0,"type":"text","text":"text"}]})");
    CHECK_THROWS(trace.queueDue(0, [](const TestInputEvent& event) { return pushTestInputEvent(event, 0); }));
    TestInputEvent oversized;
    oversized.kind = TestInputEvent::Kind::Text;
    oversized.text.assign(32, 'x');
    CHECK(pushTestInputEvent(oversized, 0) == -1);
}
