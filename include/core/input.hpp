#pragma once

#include <SDL2/SDL.h>
#include <array>
#include <glm/glm.hpp>

namespace wowee {
namespace core {

class Input {
public:
    static Input& getInstance();

    void update();

    // Only an explicit unattended trace enables this dispatch-to-poll bridge.
    void setTestReplayEnabled(bool enabled);
    void observeTestReplayEvent(const SDL_Event& event);

    // Keyboard
    [[nodiscard]] bool isKeyPressed(SDL_Scancode key) const;

    /// Holds a key down from somewhere that is not a keyboard.
    ///
    /// The on-screen stick is the reason: movement, its animations and the
    /// packets that announce it are a long chain that starts at
    /// isKeyPressed(SDL_SCANCODE_W), and a phone has no W. Setting the key here
    /// drives all of it without any of it knowing where the press came from.
    /// Merged over the hardware state, so a keyboard still works alongside.
    void setVirtualKey(SDL_Scancode key, bool held);
    void clearVirtualKeys();
    [[nodiscard]] bool isKeyJustPressed(SDL_Scancode key) const;

    // Mouse
    [[nodiscard]] bool isMouseButtonPressed(int button) const;
    [[nodiscard]] bool isMouseButtonJustPressed(int button) const;
    [[nodiscard]] bool isMouseButtonJustReleased(int button) const;

    [[nodiscard]] glm::vec2 getMousePosition() const { return mousePosition; }
    [[nodiscard]] glm::vec2 getMouseDelta() const { return mouseDelta; }

    [[nodiscard]] bool isMouseLocked() const { return mouseLocked; }

private:
    Input() = default;
    ~Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    static constexpr int NUM_KEYS = SDL_NUM_SCANCODES;
    static constexpr int NUM_MOUSE_BUTTONS = 8;

    std::array<bool, NUM_KEYS> currentKeyState = {};
    std::array<bool, NUM_KEYS> virtualKeyState = {};
    std::array<bool, NUM_KEYS> replayKeyState = {};
    bool testReplayEnabled = false;
    SDL_Keymod replayModifiers = KMOD_NONE;
    std::array<bool, NUM_KEYS> previousKeyState = {};

    std::array<bool, NUM_MOUSE_BUTTONS> currentMouseState = {};
    std::array<bool, NUM_MOUSE_BUTTONS> replayMouseState = {};
    std::array<bool, NUM_MOUSE_BUTTONS> previousMouseState = {};

    glm::vec2 mousePosition = glm::vec2(0.0f);
    glm::vec2 previousMousePosition = glm::vec2(0.0f);
    glm::vec2 mouseDelta = glm::vec2(0.0f);
    glm::vec2 replayMousePosition = glm::vec2(0.0f);
    glm::vec2 replayMouseDelta = glm::vec2(0.0f);
    bool replayMousePositionValid = false;
    bool replayMouseDeltaPending = false;
    bool suppressNextMouseDelta = false;
    bool mouseLocked = false;
};

// Scope cleanup also runs on an exception; replay never leaves SDL modifiers
// or synthetic held keys behind after an unattended run.
class TestInputReplayScope {
public:
    explicit TestInputReplayScope(bool enabled);
    ~TestInputReplayScope();
    TestInputReplayScope(const TestInputReplayScope&) = delete;
    TestInputReplayScope& operator=(const TestInputReplayScope&) = delete;
private:
    bool enabled_;
    SDL_Keymod savedModifiers_;
};

} // namespace core
} // namespace wowee
