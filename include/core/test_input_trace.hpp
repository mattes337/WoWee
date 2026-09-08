#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace wowee::core {

struct TestInputEvent {
    enum class Kind { MouseMove, MouseDown, MouseUp, MouseWheel, KeyDown, KeyUp, Text };
    uint32_t afterUpdates = 0;
    Kind kind = Kind::MouseMove;
    int32_t x = 0, y = 0, dx = 0, dy = 0;
    uint8_t button = 0;
    uint32_t buttons = 0;
    int32_t keycode = 0;
    uint16_t scancode = 0, modifiers = 0;
    std::string text;
};

// Opt-in, update-indexed SDL event stream. It does not emulate the OS's
// keyboard/mouse polling state or claim that a rendered image was presented.
class TestInputTrace {
public:
    static TestInputTrace fromFile(const char* path);
    static TestInputTrace parse(std::string_view json);
    bool enabled() const { return stopAfterUpdates_ != 0; }
    uint32_t stopAfterUpdates() const { return stopAfterUpdates_; }
    std::size_t size() const { return events_.size(); }
    const std::vector<TestInputEvent>& events() const { return events_; }

    // push must follow SDL_PushEvent's return convention: only 1 is success.
    void queueDue(uint32_t completedUpdates, const std::function<int(const TestInputEvent&)>& push);
    void requireComplete(uint32_t completedUpdates) const;

private:
    uint32_t stopAfterUpdates_ = 0;
    std::vector<TestInputEvent> events_;
    std::size_t next_ = 0;
};

// Converts to SDL's event format and queues through SDL_PushEvent. Keeps no
// separate UI or player state. Text payloads are never included in diagnostics.
int pushTestInputEvent(const TestInputEvent& event, uint32_t windowId);

} // namespace wowee::core
