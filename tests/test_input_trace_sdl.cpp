#define SDL_MAIN_HANDLED
#include <catch_amalgamated.hpp>
#include "core/test_input_trace.hpp"
#include "core/input.hpp"
#include <SDL.h>
#include <string>

using namespace wowee::core;

namespace {
struct Events {
    Events() { REQUIRE(SDL_Init(SDL_INIT_EVENTS) == 0); SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT); }
    ~Events() { SDL_SetEventFilter(nullptr, nullptr); SDL_Quit(); }
};
void dispatchReplay() {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) Input::getInstance().observeTestReplayEvent(event);
    Input::getInstance().update();
}
}

TEST_CASE("opt-in dispatched trace holds and releases movement and modifiers", "[input-trace][sdl][held]") {
    Events events;
    auto& input = Input::getInstance();
    input.update();
    auto trace = TestInputTrace::parse(R"({"version":1,"stop_after_updates":6,"events":[
        {"after_updates":0,"type":"key_down","keycode":1073742049,"scancode":225,"modifiers":1},
        {"after_updates":0,"type":"key_down","keycode":119,"scancode":26,"modifiers":1},
        {"after_updates":3,"type":"key_up","keycode":119,"scancode":26,"modifiers":1},
        {"after_updates":4,"type":"key_up","keycode":1073742049,"scancode":225,"modifiers":0}
    ]})");
    TestInputReplayScope replay(true);
    for (uint32_t update = 0; update < 6; ++update) {
        trace.queueDue(update, [](const TestInputEvent& event) { return pushTestInputEvent(event, 7); });
        dispatchReplay();
        CHECK(input.isKeyPressed(SDL_SCANCODE_W) == (update < 3));
        CHECK(input.isKeyJustPressed(SDL_SCANCODE_W) == (update == 0));
        CHECK(input.isKeyPressed(SDL_SCANCODE_LSHIFT) == (update < 4));
        CHECK(((SDL_GetModState() & KMOD_SHIFT) != 0) == (update < 4));
        // The OS polling state is untouched: the bridge is opt-in Input state.
        CHECK(SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_W] == 0);
    }
    trace.requireComplete(6);
}

TEST_CASE("replay focus loss and scope exit release keys without clearing virtual input", "[input-trace][sdl][held]") {
    Events events;
    auto& input = Input::getInstance();
    SDL_SetModState(KMOD_NUM);
    input.setVirtualKey(SDL_SCANCODE_A, true);
    {
        TestInputReplayScope replay(true);
        TestInputEvent down;
        down.kind = TestInputEvent::Kind::KeyDown;
        down.keycode = SDLK_w;
        down.scancode = SDL_SCANCODE_W;
        down.modifiers = KMOD_LCTRL;
        REQUIRE(pushTestInputEvent(down, 7) == 1);
        dispatchReplay();
        REQUIRE(input.isKeyPressed(SDL_SCANCODE_W));
        SDL_Event focus{};
        focus.type = SDL_WINDOWEVENT;
        focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        REQUIRE(SDL_PushEvent(&focus) == 1);
        dispatchReplay();
        CHECK_FALSE(input.isKeyPressed(SDL_SCANCODE_W));
        CHECK(SDL_GetModState() == KMOD_NONE);
        REQUIRE(pushTestInputEvent(down, 7) == 1);
        dispatchReplay();
    }
    input.update();
    CHECK_FALSE(input.isKeyPressed(SDL_SCANCODE_W));
    CHECK(input.isKeyPressed(SDL_SCANCODE_A));
    CHECK(SDL_GetModState() == KMOD_NUM);
    input.clearVirtualKeys();
    input.update();
}

TEST_CASE("ordinary pushed events do not enable held replay routing", "[input-trace][sdl][held]") {
    Events events;
    TestInputReplayScope replay(false);
    SDL_SetModState(KMOD_CAPS);
    TestInputEvent down;
    down.kind = TestInputEvent::Kind::KeyDown;
    down.keycode = SDLK_w;
    down.scancode = SDL_SCANCODE_W;
    down.modifiers = KMOD_LCTRL;
    REQUIRE(pushTestInputEvent(down, 7) == 1);
    dispatchReplay();
    CHECK_FALSE(Input::getInstance().isKeyPressed(SDL_SCANCODE_W));
    CHECK(SDL_GetModState() == KMOD_CAPS);

    TestInputEvent mouse;
    mouse.kind = TestInputEvent::Kind::MouseDown;
    mouse.button = SDL_BUTTON_LEFT;
    mouse.x = 999999; mouse.y = -999999;
    REQUIRE(pushTestInputEvent(mouse, 7) == 1);
    dispatchReplay();
    int physicalX = 0, physicalY = 0;
    const Uint32 physicalButtons = SDL_GetMouseState(&physicalX, &physicalY);
    CHECK(Input::getInstance().getMousePosition() ==
          glm::vec2(static_cast<float>(physicalX), static_cast<float>(physicalY)));
    CHECK(Input::getInstance().isMouseButtonPressed(SDL_BUTTON_LEFT) ==
          ((physicalButtons & SDL_BUTTON_LMASK) != 0));
}

TEST_CASE("opt-in mouse replay preserves motion, button coordinates and held state", "[input-trace][sdl][mouse]") {
    Events events;
    auto& input = Input::getInstance();
    input.update();
    TestInputReplayScope replay(true);

    TestInputEvent motion;
    motion.kind = TestInputEvent::Kind::MouseMove;
    motion.x = 120; motion.y = 80;
    motion.dx = 7; motion.dy = -3;
    motion.buttons = SDL_BUTTON_LMASK;
    REQUIRE(pushTestInputEvent(motion, 7) == 1);
    dispatchReplay();
    CHECK(input.getMousePosition() == glm::vec2(120.0f, 80.0f));
    CHECK(input.getMouseDelta() == glm::vec2(7.0f, -3.0f));
    CHECK(input.isMouseButtonPressed(SDL_BUTTON_LEFT));
    CHECK(input.isMouseButtonJustPressed(SDL_BUTTON_LEFT));

    dispatchReplay();
    CHECK(input.getMousePosition() == glm::vec2(120.0f, 80.0f));
    CHECK(input.getMouseDelta() == glm::vec2(0.0f));
    CHECK(input.isMouseButtonPressed(SDL_BUTTON_LEFT));
    CHECK_FALSE(input.isMouseButtonJustPressed(SDL_BUTTON_LEFT));

    TestInputEvent release;
    release.kind = TestInputEvent::Kind::MouseUp;
    release.button = SDL_BUTTON_LEFT;
    release.x = 125; release.y = 82;
    REQUIRE(pushTestInputEvent(release, 7) == 1);
    dispatchReplay();
    CHECK(input.getMousePosition() == glm::vec2(125.0f, 82.0f));
    CHECK(input.getMouseDelta() == glm::vec2(0.0f));
    CHECK_FALSE(input.isMouseButtonPressed(SDL_BUTTON_LEFT));
    CHECK(input.isMouseButtonJustReleased(SDL_BUTTON_LEFT));

    TestInputEvent down = release;
    down.kind = TestInputEvent::Kind::MouseDown;
    down.button = SDL_BUTTON_RIGHT;
    down.x = 130; down.y = 90;
    REQUIRE(pushTestInputEvent(down, 7) == 1);
    dispatchReplay();
    CHECK(input.getMousePosition() == glm::vec2(130.0f, 90.0f));
    CHECK(input.getMouseDelta() == glm::vec2(0.0f));
    CHECK(input.isMouseButtonPressed(SDL_BUTTON_RIGHT));
}

TEST_CASE("mouse replay focus and scope cleanup avoid synthetic cursor motion", "[input-trace][sdl][mouse]") {
    Events events;
    auto& input = Input::getInstance();
    input.update();
    {
        TestInputReplayScope replay(true);
        TestInputEvent down;
        down.kind = TestInputEvent::Kind::MouseDown;
        down.button = SDL_BUTTON_LEFT;
        down.x = 100000; down.y = -100000;
        REQUIRE(pushTestInputEvent(down, 7) == 1);
        dispatchReplay();
        REQUIRE(input.isMouseButtonPressed(SDL_BUTTON_LEFT));

        SDL_Event focus{};
        focus.type = SDL_WINDOWEVENT;
        focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        REQUIRE(SDL_PushEvent(&focus) == 1);
        dispatchReplay();
        CHECK_FALSE(input.isMouseButtonPressed(SDL_BUTTON_LEFT));
        CHECK(input.isMouseButtonJustReleased(SDL_BUTTON_LEFT));
        CHECK(input.getMousePosition() == glm::vec2(100000.0f, -100000.0f));
        CHECK(input.getMouseDelta() == glm::vec2(0.0f));
    }
    input.update();
    CHECK(input.getMouseDelta() == glm::vec2(0.0f));
    CHECK_FALSE(input.isMouseButtonPressed(SDL_BUTTON_LEFT));
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
