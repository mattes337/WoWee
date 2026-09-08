#include <catch_amalgamated.hpp>
#include "core/test_update_limit.hpp"

using wowee::core::TestUpdateLimit;

TEST_CASE("absent update limit keeps interactive execution unchanged", "[test-update-limit]") {
    TestUpdateLimit limit(nullptr);
    CHECK_FALSE(limit.enabled());
    CHECK_FALSE(limit.completeIteration());
    CHECK(limit.completed() == 0);
    CHECK_NOTHROW(limit.requireCompletedQuit(false));
}

TEST_CASE("update limit rejects nonintegers zero overflow and excessive limits", "[test-update-limit]") {
    for (const char* invalid : {"", "0", "000", "-1", "+1", " 1", "1 ", "1.0", "1e2",
                                "nan", "inf", "1000001", "4294967295", "9999999999999999999999"}) {
        CAPTURE(invalid);
        CHECK_THROWS_AS(TestUpdateLimit(invalid), std::invalid_argument);
    }
    CHECK(TestUpdateLimit("1").limit() == 1);
    CHECK(TestUpdateLimit("1000000").limit() == 1000000);
    CHECK(TestUpdateLimit("0002").limit() == 2);
}

TEST_CASE("quit is requested exactly once after the requested completed iteration", "[test-update-limit]") {
    TestUpdateLimit limit("3");
    CHECK_FALSE(limit.completeIteration());
    CHECK_FALSE(limit.completeIteration());
    CHECK(limit.completed() == 2);
    CHECK_THROWS(limit.requireCompletedQuit(true));
    CHECK(limit.completeIteration());
    CHECK(limit.reached());
    CHECK(limit.completed() == 3);
    CHECK_FALSE(limit.completeIteration());
    CHECK(limit.completed() == 3);
    CHECK_THROWS(limit.requireCompletedQuit(false));
    CHECK_NOTHROW(limit.requireCompletedQuit(true));
}

TEST_CASE("a filtered or failed quit event cannot produce smoke success", "[test-update-limit]") {
    CHECK_NOTHROW(TestUpdateLimit::requireQueuedQuit(1));
    CHECK_THROWS(TestUpdateLimit::requireQueuedQuit(0));
    CHECK_THROWS(TestUpdateLimit::requireQueuedQuit(-1));
}
