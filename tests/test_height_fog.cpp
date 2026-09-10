// The exponential height fog, calibrated against the linear ramp it replaces.
//
// Every zone in the game was authored against the linear model: the distance
// the horizon disappears at and the colour it disappears into are art, off
// Light.dbc. The rule the phase sets itself is that switching the model does
// not move the horizon - only what happens between here and there changes.
// These are that rule, as numbers.
#include <catch_amalgamated.hpp>

#include "rendering/height_fog.hpp"

using wowee::rendering::computeHeightFog;
using wowee::rendering::heightFogFactor;
using wowee::rendering::kFogBaseBelowGround;
using wowee::rendering::kFogEndTransmittance;
using wowee::rendering::kFogScaleHeight;
using wowee::rendering::linearFogFactor;

namespace {
constexpr float kGround = 100.0f;
constexpr float kFogEnd = 800.0f;
constexpr float kFogStart = 80.0f;

wowee::rendering::HeightFogParams params(float fogEnd = kFogEnd, float aerial = 0.6f) {
    return computeHeightFog(kGround, fogEnd, aerial, glm::vec3(1.0f, 0.95f, 0.8f));
}
}  // namespace

TEST_CASE("height fog puts its base below the ground under the camera") {
    const auto fog = params();
    REQUIRE(fog.fogHeight.x == Catch::Approx(kGround - kFogBaseBelowGround));
    REQUIRE(fog.fogHeight.z == Catch::Approx(1.0f / kFogScaleHeight));
}

TEST_CASE("the horizon lands where the linear model put it") {
    // The whole calibration. A horizontal ray at the fog's own base height,
    // at the zone's fog end, has to have lost what the linear ramp had lost -
    // which is everything. Two percent left is inside what an 8-bit channel
    // can show against a fog colour of any weight.
    const auto fog = params();
    const float baseZ = fog.fogHeight.x;
    const float visible = heightFogFactor(fog, baseZ, baseZ, kFogEnd);
    REQUIRE(visible == Catch::Approx(kFogEndTransmittance).margin(0.002f));
    REQUIRE(linearFogFactor(kFogStart, kFogEnd, kFogEnd) == Catch::Approx(0.0f));
}

TEST_CASE("a shorter fog end thickens the fog rather than moving the horizon") {
    // The underwater blend shortens fogEnd every frame the camera is under
    // water. If the density did not follow it, diving would move the horizon.
    for (float end : {200.0f, 400.0f, 800.0f, 1600.0f}) {
        const auto fog = params(end);
        const float baseZ = fog.fogHeight.x;
        REQUIRE(heightFogFactor(fog, baseZ, baseZ, end) ==
                Catch::Approx(kFogEndTransmittance).margin(0.002f));
    }
}

TEST_CASE("the valley is thick and the hilltop is clear") {
    const auto fog = params();
    const float baseZ = fog.fogHeight.x;
    const float inValley = heightFogFactor(fog, baseZ, baseZ, 300.0f);
    const float onARidge = heightFogFactor(fog, baseZ + 3.0f * kFogScaleHeight,
                                           baseZ + 3.0f * kFogScaleHeight, 300.0f);
    REQUIRE(onARidge > inValley);
    // Three scale heights up is e^-3 of the density, so nearly nothing is
    // left of it: this is the difference the linear model could not express
    // at any setting, and it has to be large enough to see.
    REQUIRE(onARidge > 0.9f);
    REQUIRE(inValley < 0.4f);
}

TEST_CASE("looking down from above is fogged less than looking along the valley") {
    // A ray that starts high and ends low crosses the thick air only at its
    // end, so it keeps more of the surface than a ray the same length that
    // spent all of it down there. Getting the sign of the integral wrong is
    // the classic height-fog bug and it looks like nothing until you fly.
    const auto fog = params();
    const float baseZ = fog.fogHeight.x;
    const float fromAbove = heightFogFactor(fog, baseZ + 200.0f, baseZ, 300.0f);
    const float alongTheFloor = heightFogFactor(fog, baseZ, baseZ, 300.0f);
    REQUIRE(fromAbove > alongTheFloor);
}

TEST_CASE("the integral is continuous through a level ray") {
    // The closed form divides by the height difference, and a camera level
    // with what it is looking at is the common case rather than a corner one.
    // The limit is taken directly below a threshold; either side of that
    // threshold has to agree, or a slow pitch of the camera flickers.
    const auto fog = params();
    const float baseZ = fog.fogHeight.x;
    const float justBelow = heightFogFactor(fog, baseZ, baseZ + 0.05f, 300.0f);
    const float exactly = heightFogFactor(fog, baseZ, baseZ, 300.0f);
    const float justAbove = heightFogFactor(fog, baseZ, baseZ - 0.05f, 300.0f);
    REQUIRE(justBelow == Catch::Approx(exactly).margin(0.002f));
    REQUIRE(justAbove == Catch::Approx(exactly).margin(0.002f));
}

TEST_CASE("standing far below the base does not white out the screen") {
    // exp() of a large positive number is an infinity, and an infinite optical
    // depth is a screen that is entirely fog colour. The bottom of a mine and
    // the sea floor are both hundreds of yards below the ground the fog was
    // calibrated at.
    const auto fog = params();
    const float deep = fog.fogHeight.x - 2000.0f;
    const float visible = heightFogFactor(fog, deep, deep, 50.0f);
    REQUIRE(std::isfinite(visible));
    REQUIRE(visible >= 0.0f);
    REQUIRE(visible <= 1.0f);
}

TEST_CASE("nothing at all is fully visible, and everything is bounded") {
    const auto fog = params();
    const float baseZ = fog.fogHeight.x;
    REQUIRE(heightFogFactor(fog, baseZ, baseZ, 0.0f) == Catch::Approx(1.0f));
    for (float d = 0.0f; d <= 4000.0f; d += 97.0f) {
        const float f = heightFogFactor(fog, baseZ, baseZ + 40.0f, d);
        REQUIRE(f >= 0.0f);
        REQUIRE(f <= 1.0f);
    }
}

TEST_CASE("a zone with no fog end falls back rather than dividing by nothing") {
    const auto fog = computeHeightFog(kGround, 0.0f, 0.6f, glm::vec3(1.0f));
    REQUIRE(std::isfinite(fog.fogHeight.y));
    REQUIRE(fog.fogHeight.y > 0.0f);
}

TEST_CASE("the aerial strength is the setting, clamped") {
    REQUIRE(params(kFogEnd, 0.6f).fogHeight.w == Catch::Approx(0.6f));
    REQUIRE(params(kFogEnd, -1.0f).fogHeight.w == Catch::Approx(0.0f));
    REQUIRE(params(kFogEnd, 5.0f).fogHeight.w == Catch::Approx(1.0f));
}

TEST_CASE("the in-scatter colour is the zone's own sun colour, unchanged") {
    // The point of deriving it rather than choosing one: an authored zone's
    // sun is its own, and a fog that glows a colour nobody authored is a zone
    // that has shifted hue - which is the thing the plan forbids.
    const glm::vec3 sun(0.93f, 0.71f, 0.42f);
    const auto fog = computeHeightFog(kGround, kFogEnd, 0.5f, sun);
    REQUIRE(fog.fogSunColor.r == Catch::Approx(sun.r));
    REQUIRE(fog.fogSunColor.g == Catch::Approx(sun.g));
    REQUIRE(fog.fogSunColor.b == Catch::Approx(sun.b));
}
