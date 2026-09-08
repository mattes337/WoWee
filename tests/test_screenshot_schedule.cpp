#include <catch_amalgamated.hpp>
#include "core/test_screenshot_schedule.hpp"
using wowee::core::TestScreenshotSchedule;

TEST_CASE("screenshot schedule defaults to first frame and queues only once", "[screenshot]") {
    TestScreenshotSchedule disabled(nullptr, nullptr, 0);
    CHECK_FALSE(disabled.takeDue());
    TestScreenshotSchedule first("capture.png", nullptr, 120);
    CHECK(first.takeDue());
    CHECK_FALSE(first.takeDue());
    first.completeIteration();
    CHECK_FALSE(first.takeDue());
}
TEST_CASE("screenshot schedule counts only completed iterations", "[screenshot]") {
    TestScreenshotSchedule later("capture.png", "2", 3);
    CHECK_FALSE(later.takeDue());
    CHECK_FALSE(later.takeDue()); // skipped update does not advance
    later.completeIteration();
    CHECK_FALSE(later.takeDue());
    later.completeIteration();
    CHECK(later.takeDue());
    CHECK_FALSE(later.takeDue());
    later.completeIteration();
    CHECK_FALSE(later.takeDue());
}
TEST_CASE("screenshot schedule rejects invalid input and unreachable bounds", "[screenshot]") {
    for (const char* bad : {"", "-1", "+1", " 1", "1 ", "1.0", "1000001", "999999999999"})
        CHECK_THROWS_AS(TestScreenshotSchedule("capture.png", bad, 0), std::invalid_argument);
    CHECK_THROWS_AS(TestScreenshotSchedule(nullptr, "0", 120), std::invalid_argument);
    CHECK_THROWS_AS(TestScreenshotSchedule("", nullptr, 120), std::invalid_argument);
    CHECK_THROWS_AS(TestScreenshotSchedule("capture.png", "120", 120), std::invalid_argument);
    CHECK_THROWS_AS(TestScreenshotSchedule("capture.png", "121", 120), std::invalid_argument);
    CHECK_NOTHROW(TestScreenshotSchedule("capture.png", "0", 1));
    CHECK_NOTHROW(TestScreenshotSchedule("capture.png", "1000000", 0));
}
