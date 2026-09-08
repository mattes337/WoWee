#include "core/test_input_trace.hpp"
#include <SDL.h>
#include <cstring>

namespace wowee::core {

int pushTestInputEvent(const TestInputEvent& input, uint32_t windowId) {
    SDL_Event event{};
    using Kind = TestInputEvent::Kind;
    switch (input.kind) {
    case Kind::MouseMove:
        event.type = SDL_MOUSEMOTION;
        event.motion.windowID = windowId;
        event.motion.state = input.buttons;
        event.motion.x = input.x; event.motion.y = input.y;
        event.motion.xrel = input.dx; event.motion.yrel = input.dy;
        break;
    case Kind::MouseDown:
    case Kind::MouseUp:
        event.type = input.kind == Kind::MouseDown ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
        event.button.windowID = windowId;
        event.button.button = input.button;
        event.button.state = input.kind == Kind::MouseDown ? SDL_PRESSED : SDL_RELEASED;
        event.button.clicks = 1;
        event.button.x = input.x; event.button.y = input.y;
        break;
    case Kind::MouseWheel:
        event.type = SDL_MOUSEWHEEL;
        event.wheel.windowID = windowId;
        event.wheel.x = input.x; event.wheel.y = input.y;
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2, 0, 18)
        event.wheel.preciseX = static_cast<float>(input.x);
        event.wheel.preciseY = static_cast<float>(input.y);
#endif
        break;
    case Kind::KeyDown:
    case Kind::KeyUp:
        event.type = input.kind == Kind::KeyDown ? SDL_KEYDOWN : SDL_KEYUP;
        event.key.windowID = windowId;
        event.key.state = input.kind == Kind::KeyDown ? SDL_PRESSED : SDL_RELEASED;
        event.key.keysym.sym = static_cast<SDL_Keycode>(input.keycode);
        event.key.keysym.scancode = static_cast<SDL_Scancode>(input.scancode);
        event.key.keysym.mod = input.modifiers;
        break;
    case Kind::Text:
        if (input.text.empty() || input.text.size() >= SDL_TEXTINPUTEVENT_TEXT_SIZE ||
            input.text.find('\0') != std::string::npos) return -1;
        event.type = SDL_TEXTINPUT;
        event.text.windowID = windowId;
        std::memcpy(event.text.text, input.text.data(), input.text.size());
        break;
    default:
        return -1;
    }
    return SDL_PushEvent(&event);
}

} // namespace wowee::core
