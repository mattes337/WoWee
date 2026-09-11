// What a glue screen says about its scene, turned into what the shader takes.
//
// GlueParent.lua's SetLighting hands a model a fog range, a fog colour and up
// to four directional lights out of RaceLights. This client's per-frame block
// has room for one directional light, one ambient colour and one fog range, so
// there is arithmetic in between - and every way of getting it wrong still
// renders. A merge that drops the ambient term makes a scene lit only where it
// faces one way; a fog range whose end is not past its start divides by zero
// in the shader and paints the whole scene a NaN. Neither reads as "the
// lighting is wrong", which is why they are worth a test.

#include "catch_amalgamated.hpp"
#include "rendering/glue_scene.hpp"

#include <cmath>
#include <vector>

using wowee::rendering::GlueSceneLight;
using wowee::rendering::glueCameraIndex;
using wowee::rendering::glueSceneFogRange;
using wowee::rendering::glueSceneLighting;

namespace {

/// One row of a RaceLights table, in the order GlueParent unpacks it into
/// AddLight: direction, ambient intensity and colour, diffuse intensity and
/// colour. The enabled flag and the light type are dropped before this point.
GlueSceneLight makeLight(float dx, float dy, float dz,
                         float ambI, float ambR, float ambG, float ambB,
                         float difI, float difR, float difG, float difB) {
    GlueSceneLight light;
    light.direction[0] = dx;
    light.direction[1] = dy;
    light.direction[2] = dz;
    light.ambientIntensity = ambI;
    light.ambientColor[0] = ambR;
    light.ambientColor[1] = ambG;
    light.ambientColor[2] = ambB;
    light.diffuseIntensity = difI;
    light.diffuseColor[0] = difR;
    light.diffuseColor[1] = difG;
    light.diffuseColor[2] = difB;
    return light;
}

}  // namespace

TEST_CASE("a screen that added no lights is not lit by this") {
    const auto lit = glueSceneLighting(nullptr, 0);
    CHECK_FALSE(lit.authored);

    // Not merely "the answer is black". The caller has to be able to tell
    // "nothing was said" from "it was lit black", because the first means keep
    // the rig you already have and the second means the scene is dark - and
    // the stock login screen never calls SetLighting at all, so the first is
    // the ordinary case rather than the exception.
    std::vector<GlueSceneLight> none;
    CHECK_FALSE(glueSceneLighting(none.data(), none.size()).authored);
}

TEST_CASE("the death knight lighting is ambient and nothing else") {
    // RaceLights.DEATHKNIGHT, which is what the login screen's own override
    // asks for: one light, straight down, a blue ambient and a black diffuse.
    const GlueSceneLight dk = makeLight(0.0f, 0.0f, -1.0f,
                                        1.0f, 0.38824f, 0.66353f, 0.76941f,
                                        1.0f, 0.0f, 0.0f, 0.0f);
    const auto lit = glueSceneLighting(&dk, 1);
    REQUIRE(lit.authored);
    CHECK(std::abs(lit.ambientColor[0] - 0.38824f) < 1e-5f);
    CHECK(std::abs(lit.ambientColor[1] - 0.66353f) < 1e-5f);
    CHECK(std::abs(lit.ambientColor[2] - 0.76941f) < 1e-5f);
    // A black diffuse is not a light coming from anywhere, so nothing is added
    // to the directional term - the scene reads as evenly lit, which is what
    // that screen looks like.
    CHECK(lit.lightColor[0] == 0.0f);
    CHECK(lit.lightColor[1] == 0.0f);
    CHECK(lit.lightColor[2] == 0.0f);
}

TEST_CASE("ambient and diffuse are scaled by their own intensities") {
    // Halve the intensities and the colours halve with them. Applying the
    // intensity to the wrong one of the pair passes every test that only
    // checks a single light with both set to one, which every row in
    // RaceLights but a handful is.
    const GlueSceneLight light = makeLight(0.0f, 0.0f, -1.0f,
                                           0.5f, 0.4f, 0.4f, 0.4f,
                                           2.0f, 0.1f, 0.2f, 0.3f);
    const auto lit = glueSceneLighting(&light, 1);
    REQUIRE(lit.authored);
    CHECK(std::abs(lit.ambientColor[0] - 0.2f) < 1e-5f);
    CHECK(std::abs(lit.lightColor[0] - 0.2f) < 1e-5f);
    CHECK(std::abs(lit.lightColor[1] - 0.4f) < 1e-5f);
    CHECK(std::abs(lit.lightColor[2] - 0.6f) < 1e-5f);
}

TEST_CASE("the human backdrop's three lights merge into one and an ambient") {
    // RaceLights.HUMAN: a pure ambient grey, and two coloured lights from
    // opposite sides.
    const std::vector<GlueSceneLight> human = {
        makeLight(0.0f, 0.0f, -1.0f, 1.0f, 0.27f, 0.27f, 0.27f, 1.0f, 0.0f, 0.0f, 0.0f),
        makeLight(-0.45756075f, -0.58900136f, -0.66611975f,
                  1.0f, 0.0f, 0.0f, 0.0f,
                  1.0f, 0.19882353f, 0.34921569f, 0.43588236f),
        makeLight(-0.64623469f, 0.57582057f, -0.50081086f,
                  1.0f, 0.0f, 0.0f, 0.0f,
                  2.0f, 0.52196085f, 0.44f, 0.29764709f),
    };
    const auto lit = glueSceneLighting(human.data(), human.size());
    REQUIRE(lit.authored);

    // The ambient is the first light's and only the first light's: the other
    // two carry a black ambient.
    CHECK(std::abs(lit.ambientColor[0] - 0.27f) < 1e-5f);
    CHECK(std::abs(lit.ambientColor[1] - 0.27f) < 1e-5f);
    CHECK(std::abs(lit.ambientColor[2] - 0.27f) < 1e-5f);

    // The light colour is the sum of what the two coloured lights contribute,
    // the second at double intensity.
    CHECK(std::abs(lit.lightColor[0] - (0.19882353f + 2.0f * 0.52196085f)) < 1e-5f);
    CHECK(std::abs(lit.lightColor[1] - (0.34921569f + 2.0f * 0.44f)) < 1e-5f);
    CHECK(std::abs(lit.lightColor[2] - (0.43588236f + 2.0f * 0.29764709f)) < 1e-5f);

    // Whatever the direction ends up as, it is a unit vector - the shader
    // normalises nothing and a direction of length two doubles the light.
    const float len = std::sqrt(lit.direction[0] * lit.direction[0] +
                                lit.direction[1] * lit.direction[1] +
                                lit.direction[2] * lit.direction[2]);
    CHECK(std::abs(len - 1.0f) < 1e-4f);

    // And it leans toward the brighter of the two, which is the one at double
    // intensity - the one with positive Y.
    CHECK(lit.direction[1] > 0.0f);
    CHECK(lit.direction[2] < 0.0f);
}

TEST_CASE("a light with no colour does not drag the direction") {
    // The first row of nearly every RaceLights table is a pure ambient light
    // pointing straight down. Weighting the merge by intensity alone would let
    // it pull the direction to straight down whatever the real lights say, and
    // the scene would be lit from above however it was authored.
    const std::vector<GlueSceneLight> lights = {
        makeLight(0.0f, 0.0f, -1.0f, 1.0f, 0.2f, 0.2f, 0.2f, 1.0f, 0.0f, 0.0f, 0.0f),
        makeLight(1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.8f, 0.8f, 0.8f),
    };
    const auto lit = glueSceneLighting(lights.data(), lights.size());
    REQUIRE(lit.authored);
    CHECK(std::abs(lit.direction[0] - 1.0f) < 1e-5f);
    CHECK(std::abs(lit.direction[2]) < 1e-5f);
}

TEST_CASE("a direction that is not a direction is skipped, not divided by") {
    const std::vector<GlueSceneLight> lights = {
        makeLight(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f),
        makeLight(0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f),
    };
    const auto lit = glueSceneLighting(lights.data(), lights.size());
    REQUIRE(lit.authored);
    CHECK(std::isfinite(lit.direction[0]));
    CHECK(std::abs(lit.direction[1] - 1.0f) < 1e-5f);
}

TEST_CASE("fog off is a range nothing in the scene reaches") {
    // Not a flag: the shader always mixes, so "no fog" has to be expressible
    // as a range. Both ends finite and the end past the start, or the mix
    // itself is the fault.
    const auto off = glueSceneFogRange(false, 0.0f, 1200.0f);
    CHECK(off.start > 1000.0f);
    CHECK(off.end > off.start);
    CHECK(std::isfinite(off.start));
    CHECK(std::isfinite(off.end));
}

TEST_CASE("the login screen's declared fog range is used as declared") {
    // AccountLogin.xml: fogNear="0" fogFar="1200".
    const auto fog = glueSceneFogRange(true, 0.0f, 1200.0f);
    CHECK(fog.start == 0.0f);
    CHECK(fog.end == 1200.0f);
}

TEST_CASE("a fog range that would divide by zero is refused") {
    // SetFogNear without SetFogFar is a legal thing for a screen to say and
    // leaves the far distance at zero. (end - start) is then zero, and the
    // shader's fog factor is a NaN over every pixel of the scene - which
    // renders as a scene that has vanished rather than as fog set wrongly.
    SECTION("no range at all") {
        const auto fog = glueSceneFogRange(true, 0.0f, 0.0f);
        CHECK(fog.end > fog.start);
        CHECK(fog.start > 1000.0f);
    }
    SECTION("the far distance in front of the near one") {
        const auto fog = glueSceneFogRange(true, 500.0f, 100.0f);
        CHECK(fog.end > fog.start);
        CHECK(fog.start > 1000.0f);
    }
    SECTION("values that are not numbers") {
        const auto fog = glueSceneFogRange(true, std::nanf(""), 1200.0f);
        CHECK(std::isfinite(fog.start));
        CHECK(fog.end > fog.start);
    }
}

TEST_CASE("SetCamera picks the camera it names when the model has one") {
    CHECK(glueCameraIndex(0, 1) == 0);
    CHECK(glueCameraIndex(1, 3) == 1);
    CHECK(glueCameraIndex(2, 3) == 2);
}

TEST_CASE("a camera the model does not have falls back to the first") {
    // Slightly wrong framing beats no scene at all: the fallback is the camera
    // every glue screen actually asks for.
    CHECK(glueCameraIndex(3, 3) == 0);
    CHECK(glueCameraIndex(-1, 3) == 0);
}

TEST_CASE("a model with no cameras cannot be framed") {
    // Nothing else in the interface says where to stand, so this has to be
    // distinguishable from "the first one" rather than clamped into it.
    CHECK(glueCameraIndex(0, 0) < 0);
    CHECK(glueCameraIndex(2, 0) < 0);
}
