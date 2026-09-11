// The arithmetic that places the login and character-screen backdrops.
//
// A glue backdrop is an M2 scene framed by the camera its artist baked into
// it, and there is nothing else in the interface that says where to stand. Get
// either conversion wrong and the scene still renders - which is why it is
// worth a test: a wrong field of view looks like a model placed too far away,
// and a wrong pitch looks like the wrong model, so neither reads as the sum it
// actually is.

#include "catch_amalgamated.hpp"
#include "rendering/glue_scene.hpp"

#include <cmath>

using wowee::rendering::glueSceneFraming;

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

TEST_CASE("a camera looking along -X is a half turn of yaw and no pitch") {
    const float eye[3] = {11.1171f, -0.0350333f, 2.44149f};
    const float at[3] = {-10.2846f, -0.0350333f, 2.44149f};
    const auto f = glueSceneFraming(eye, at, 1.4f, 16.0f / 9.0f);
    REQUIRE(f.usable);
    CHECK(std::abs(std::abs(f.yawDegrees) - 180.0f) < 0.01f);
    CHECK(std::abs(f.pitchDegrees) < 0.01f);
}

TEST_CASE("pitch is the rise of the look direction, not its angle in the plane") {
    // One unit along +X and one unit up: 45 degrees above the horizon.
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float at[3] = {1.0f, 0.0f, 1.0f};
    const auto f = glueSceneFraming(eye, at, 1.4f, 1.0f);
    REQUIRE(f.usable);
    CHECK(std::abs(f.yawDegrees) < 0.01f);
    CHECK(std::abs(f.pitchDegrees - 45.0f) < 0.01f);

    // And below it, so a sign error cannot pass.
    const float below[3] = {1.0f, 0.0f, -1.0f};
    const auto g = glueSceneFraming(eye, below, 1.4f, 1.0f);
    REQUIRE(g.usable);
    CHECK(std::abs(g.pitchDegrees + 45.0f) < 0.01f);
}

TEST_CASE("yaw runs from +X toward +Y") {
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float at[3] = {0.0f, 1.0f, 0.0f};
    const auto f = glueSceneFraming(eye, at, 1.4f, 1.0f);
    REQUIRE(f.usable);
    CHECK(std::abs(f.yawDegrees - 90.0f) < 0.01f);
}

TEST_CASE("the field of view is converted from diagonal to vertical") {
    const float eye[3] = {0.0f, 0.0f, 0.0f};
    const float at[3] = {1.0f, 0.0f, 0.0f};

    // A square view is the one case where the two are closest, and even there
    // the diagonal is sqrt(2) wider than the vertical.
    const auto square = glueSceneFraming(eye, at, 1.4f, 1.0f);
    REQUIRE(square.usable);
    const float expectedSquare = 1.4f / std::sqrt(2.0f) * 180.0f / kPi;
    CHECK(std::abs(square.fovYDegrees - expectedSquare) < 0.01f);

    // The wider the view, the further the two diverge - so a build that passed
    // the diagonal straight through would be most wrong where it matters most,
    // on a full-screen backdrop.
    const auto wide = glueSceneFraming(eye, at, 1.4f, 16.0f / 9.0f);
    REQUIRE(wide.usable);
    CHECK(wide.fovYDegrees < square.fovYDegrees);
    const float expectedWide =
        1.4f / std::sqrt(1.0f + (16.0f / 9.0f) * (16.0f / 9.0f)) * 180.0f / kPi;
    CHECK(std::abs(wide.fovYDegrees - expectedWide) < 0.01f);
}

TEST_CASE("a camera that cannot frame anything says so") {
    const float eye[3] = {1.0f, 2.0f, 3.0f};

    SECTION("looking at the point it stands on") {
        const float at[3] = {1.0f, 2.0f, 3.0f};
        CHECK_FALSE(glueSceneFraming(eye, at, 1.4f, 1.0f).usable);
    }
    SECTION("no field of view at all") {
        const float at[3] = {2.0f, 2.0f, 3.0f};
        CHECK_FALSE(glueSceneFraming(eye, at, 0.0f, 1.0f).usable);
    }
    SECTION("a field of view larger than a camera can have") {
        const float at[3] = {2.0f, 2.0f, 3.0f};
        CHECK_FALSE(glueSceneFraming(eye, at, 6.0f, 1.0f).usable);
    }
    SECTION("a view with no width") {
        const float at[3] = {2.0f, 2.0f, 3.0f};
        CHECK_FALSE(glueSceneFraming(eye, at, 1.4f, 0.0f).usable);
    }
    SECTION("values that are not numbers") {
        const float at[3] = {std::nanf(""), 2.0f, 3.0f};
        CHECK_FALSE(glueSceneFraming(eye, at, 1.4f, 1.0f).usable);
    }
}
