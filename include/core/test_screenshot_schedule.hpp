#pragma once
#include <cstdint>
#include <stdexcept>

namespace wowee::core {
// Test-only completed-update schedule, not a presented-frame/server-state wait.
class TestScreenshotSchedule {
public:
    TestScreenshotSchedule(const char* path, const char* setting, uint32_t stop) : enabled_(path != nullptr) {
        if ((path && !*path) || (setting && !path)) invalid();
        if (setting) {
            if (!*setting) invalid();
            for (const char* p = setting; *p; ++p) {
                if (*p < '0' || *p > '9') invalid();
                const auto digit = static_cast<uint32_t>(*p - '0');
                if (after_ > (1000000u - digit) / 10) invalid();
                after_ = after_ * 10 + digit;
            }
        }
        if (enabled_ && stop && after_ >= stop) invalid();
    }
    bool takeDue() {
        if (!enabled_ || queued_ || completed_ < after_) return false;
        queued_ = true;
        return true;
    }
    void completeIteration() { if (completed_ < after_) ++completed_; }
    uint32_t afterUpdates() const { return after_; }
private:
    [[noreturn]] static void invalid() {
        throw std::invalid_argument("WOWEE_TEST_SCREENSHOT_AFTER_UPDATES requires a nonempty screenshot path and integer 0..1000000 below the bounded stop");
    }
    bool enabled_ = false;
    bool queued_ = false;
    uint32_t after_ = 0;
    uint32_t completed_ = 0;
};
}
