#include "core/input.hpp"

namespace wowee {
namespace core {

Input& Input::getInstance() {
    static Input instance;
    return instance;
}

void Input::update() {
    // Copy current state to previous
    previousKeyState = currentKeyState;
    previousMouseState = currentMouseState;
    previousMousePosition = mousePosition;

    // Get current keyboard state
    const Uint8* keyState = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < NUM_KEYS; ++i) {
        currentKeyState[i] = keyState[i] || virtualKeyState[i] || replayKeyState[i];
    }

    // Get current mouse state
    int mouseX, mouseY;
    Uint32 mouseState = SDL_GetMouseState(&mouseX, &mouseY);
    mousePosition = replayMousePositionValid
        ? replayMousePosition
        : glm::vec2(static_cast<float>(mouseX), static_cast<float>(mouseY));

    // SDL_BUTTON(x) is defined as (1 << (x-1)), so button indices are 1-based.
    // SDL_BUTTON(0) is undefined behavior (negative shift). Start at 1.
    currentMouseState[0] = false;
    for (int i = 1; i < NUM_MOUSE_BUTTONS; ++i) {
        currentMouseState[i] = (mouseState & SDL_BUTTON(i)) != 0 || replayMouseState[i];
    }

    // Calculate mouse delta
    if (suppressNextMouseDelta) {
        mouseDelta = glm::vec2(0.0f);
        suppressNextMouseDelta = false;
    } else if (replayMouseDeltaPending) {
        mouseDelta = replayMouseDelta;
    } else {
        mouseDelta = mousePosition - previousMousePosition;
    }
    replayMouseDelta = glm::vec2(0.0f);
    replayMouseDeltaPending = false;
}

void Input::setTestReplayEnabled(bool enabled) {
    const bool returningToPhysicalMouse = testReplayEnabled && !enabled && replayMousePositionValid;
    testReplayEnabled = enabled;
    replayKeyState.fill(false);
    replayModifiers = KMOD_NONE;
    replayMouseState.fill(false);
    replayMousePositionValid = false;
    replayMouseDelta = glm::vec2(0.0f);
    replayMouseDeltaPending = false;
    suppressNextMouseDelta = returningToPhysicalMouse;
}

void Input::observeTestReplayEvent(const SDL_Event& event) {
    if (!testReplayEnabled) return;
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        const auto key = event.key.keysym.scancode;
        if (key > SDL_SCANCODE_UNKNOWN && key < SDL_NUM_SCANCODES)
            replayKeyState[key] = event.type == SDL_KEYDOWN;
        replayModifiers = static_cast<SDL_Keymod>(event.key.keysym.mod);
    } else if (event.type == SDL_MOUSEMOTION) {
        replayMousePosition = glm::vec2(static_cast<float>(event.motion.x),
                                        static_cast<float>(event.motion.y));
        replayMousePositionValid = true;
        replayMouseDelta += glm::vec2(static_cast<float>(event.motion.xrel),
                                      static_cast<float>(event.motion.yrel));
        replayMouseDeltaPending = true;
        for (int i = 1; i < NUM_MOUSE_BUTTONS; ++i)
            replayMouseState[i] = (event.motion.state & SDL_BUTTON(i)) != 0;
    } else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        replayMousePosition = glm::vec2(static_cast<float>(event.button.x),
                                        static_cast<float>(event.button.y));
        replayMousePositionValid = true;
        // A button's x/y identifies the click; it is not a motion event.
        if (!replayMouseDeltaPending) {
            replayMouseDelta = glm::vec2(0.0f);
            replayMouseDeltaPending = true;
        }
        if (event.button.button > 0 && event.button.button < NUM_MOUSE_BUTTONS)
            replayMouseState[event.button.button] = event.type == SDL_MOUSEBUTTONDOWN;
    } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
        replayKeyState.fill(false);
        replayModifiers = KMOD_NONE;
        replayMouseState.fill(false);
        replayMouseDelta = glm::vec2(0.0f);
        replayMouseDeltaPending = replayMousePositionValid;
    }
    // Existing C++/Lua modifier queries use SDL_GetModState. SDL_PushEvent
    // alone does not update it, so synchronize only during opt-in dispatch.
    SDL_SetModState(replayModifiers);
}

TestInputReplayScope::TestInputReplayScope(bool enabled)
    : enabled_(enabled), savedModifiers_(enabled ? SDL_GetModState() : KMOD_NONE) {
    if (enabled_) {
        Input::getInstance().setTestReplayEnabled(true);
        SDL_SetModState(KMOD_NONE);
    }
}

TestInputReplayScope::~TestInputReplayScope() {
    if (enabled_) {
        Input::getInstance().setTestReplayEnabled(false);
        SDL_SetModState(savedModifiers_);
    }
}

void Input::setVirtualKey(SDL_Scancode key, bool held) {
    if (key < 0 || key >= NUM_KEYS) return;
    virtualKeyState[key] = held;
}

void Input::clearVirtualKeys() {
    virtualKeyState.fill(false);
}

bool Input::isKeyPressed(SDL_Scancode key) const {
    if (key < 0 || key >= NUM_KEYS) return false;
    return currentKeyState[key];
}

bool Input::isKeyJustPressed(SDL_Scancode key) const {
    if (key < 0 || key >= NUM_KEYS) return false;
    return currentKeyState[key] && !previousKeyState[key];
}
bool Input::isMouseButtonPressed(int button) const {
    if (button < 0 || button >= NUM_MOUSE_BUTTONS) return false;
    return currentMouseState[button];
}

bool Input::isMouseButtonJustPressed(int button) const {
    if (button < 0 || button >= NUM_MOUSE_BUTTONS) return false;
    return currentMouseState[button] && !previousMouseState[button];
}

bool Input::isMouseButtonJustReleased(int button) const {
    if (button < 0 || button >= NUM_MOUSE_BUTTONS) return false;
    return !currentMouseState[button] && previousMouseState[button];
}
} // namespace core
} // namespace wowee
