#include <catch_amalgamated.hpp>
#include "rendering/screenshot_request.hpp"
using namespace wowee::rendering;

TEST_CASE("screenshot request remains pending until explicit completion", "[screenshot]") {
    ScreenshotRequest request;
    CHECK(request.result() == ScreenshotResult::Idle);
    CHECK_FALSE(request.queue(""));
    CHECK(request.queue("one.png"));
    CHECK(request.result() == ScreenshotResult::Pending);
    CHECK_FALSE(request.queue("two.png"));
    CHECK(request.path() == "one.png");
    request.complete(true);
    CHECK(request.result() == ScreenshotResult::Succeeded);
    request.complete(false);
    CHECK(request.result() == ScreenshotResult::Succeeded);
    CHECK(request.queue("two.png"));
    request.complete(false);
    CHECK(request.result() == ScreenshotResult::Failed);
}
TEST_CASE("shutdown cancels only a pending screenshot", "[screenshot]") {
    ScreenshotRequest request;
    request.cancel();
    CHECK(request.result() == ScreenshotResult::Idle);
    REQUIRE(request.queue("pending.png"));
    request.cancel();
    CHECK(request.result() == ScreenshotResult::Cancelled);
    request.complete(true);
    CHECK(request.result() == ScreenshotResult::Cancelled);
    REQUIRE(request.queue("next.png"));
    request.complete(true);
    request.cancel();
    CHECK(request.result() == ScreenshotResult::Succeeded);
}
