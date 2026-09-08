#pragma once

#include <cstdint>
#include <stdexcept>

namespace wowee::core {

// Counts completed application update/render iterations, not presented frames.
// An absent setting leaves ordinary interactive execution unchanged.
class TestUpdateLimit {
public:
    explicit TestUpdateLimit(const char* setting) {
        if (!setting) return;
        if (!*setting) invalidSetting();
        for (const char* p = setting; *p; ++p) {
            if (*p < '0' || *p > '9') invalidSetting();
            const auto digit = static_cast<uint32_t>(*p - '0');
            if (limit_ > (kMaximum - digit) / 10) invalidSetting();
            limit_ = limit_ * 10 + digit;
        }
        if (limit_ == 0) invalidSetting();
    }

    bool enabled() const { return limit_ != 0; }
    uint32_t limit() const { return limit_; }
    uint32_t completed() const { return completed_; }
    bool reached() const { return enabled() && completed_ == limit_; }

    bool completeIteration() {
        if (!enabled() || reached()) return false;
        return ++completed_ == limit_;
    }

    static void requireQueuedQuit(int pushResult) {
        // SDL_PushEvent returns 1 on success, 0 if filtered and -1 on error.
        if (pushResult != 1)
            throw std::runtime_error("WOWEE_TEST_MAX_UPDATES: SDL_QUIT was rejected");
    }

    void requireCompletedQuit(bool quitDispatched) const {
        if (enabled() && (!reached() || !quitDispatched))
            throw std::runtime_error("WOWEE_TEST_MAX_UPDATES: loop ended before the requested quit was dispatched");
    }

private:
    [[noreturn]] static void invalidSetting() {
        throw std::invalid_argument("WOWEE_TEST_MAX_UPDATES must be an integer from 1 to 1000000");
    }
    static constexpr uint32_t kMaximum = 1000000;
    uint32_t limit_ = 0;
    uint32_t completed_ = 0;
};

} // namespace wowee::core
