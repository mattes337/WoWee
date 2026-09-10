#pragma once

/**
 * height_fog.hpp - the exponential fog's parameters, derived from the zone's own.
 *
 * The client's fog is a linear ramp between two distances off Light.dbc, and
 * every zone in the game was authored against it: the distance the horizon
 * disappears at, and the colour it disappears into, are art. Exponential
 * height fog cannot be "turned on" over that without changing every zone's
 * look, which is the one thing the modern-rendering plan will not do.
 *
 * So the exponential model is *calibrated* to the linear one rather than
 * replacing it. The density is chosen so that a horizontal ray at the fog's
 * base height has lost the same amount at `fogEnd` as the linear ramp had -
 * the horizon lands where the artist put it - and the height falloff is what
 * adds the thing the linear model could not say: that the valley is thick and
 * the hilltop is clear.
 *
 * A free function over plain numbers, so the calibration can be tested without
 * a device, a zone or a frame. `tests/test_height_fog.cpp` is that test.
 */

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace wowee {
namespace rendering {

/// What the shader reads: the two vec4s appended to the per-frame block.
struct HeightFogParams {
    /// x = base height in world Z, y = density at that height per yard,
    /// z = 1 / scale height, w = aerial-perspective strength.
    glm::vec4 fogHeight{0.0f};
    /// rgb = the colour the sun scatters into the air, w unused.
    glm::vec4 fogSunColor{0.0f};
};

/// How much of a surface at `fogEnd` the linear ramp leaves visible: none.
///
/// The exponential model never reaches zero, so "the same amount" has to be a
/// number rather than the limit. Two percent is below what an 8-bit channel
/// can hold against a fog colour of any weight - a 0.02 mix is at most half a
/// code value away from the linear model's answer, which is far inside the
/// ΔE 3 the phase's calibration rule asks for.
inline constexpr float kFogEndTransmittance = 0.02f;

/// Yards of height over which the fog thins by a factor of e.
///
/// Sixty, which is about the drop from Duskwood's road to the valley floor
/// and about the height of Stormwind's outer wall: low enough that standing
/// on the wall puts the player above the worst of it, high enough that a
/// building's upper floor is not suddenly clear air.
inline constexpr float kFogScaleHeight = 60.0f;

/// How far below the ground under the camera the fog's base sits.
inline constexpr float kFogBaseBelowGround = 20.0f;

/// The parameters for one frame.
///
/// `groundZ` is the terrain height under the camera; the base sits twenty
/// yards below it, so a player standing in a valley is inside the thick part
/// and a player on a ridge is above it. `fogEnd` is the zone's own, after
/// whatever the frame did to it - the underwater blend shortens it, and the
/// calibration has to follow that or the horizon moves when you dive.
///
/// `sunColor` is the zone's own directional colour, unchanged: the in-scatter
/// term is the sun seen through air, so its hue is the sun's.
inline HeightFogParams computeHeightFog(float groundZ, float fogEnd, float aerialStrength,
                                        const glm::vec3& sunColor) {
    HeightFogParams out;
    // A zone with no fog end - or a frame before the lighting is up - gets a
    // density of zero, which the shader reads as "fall back to linear".
    const float end = std::max(fogEnd, 1.0f);
    const float opticalDepthAtEnd = -std::log(kFogEndTransmittance);  // ~3.912

    out.fogHeight.x = groundZ - kFogBaseBelowGround;
    out.fogHeight.y = opticalDepthAtEnd / end;
    out.fogHeight.z = 1.0f / kFogScaleHeight;
    out.fogHeight.w = std::clamp(aerialStrength, 0.0f, 1.0f);
    out.fogSunColor = glm::vec4(sunColor, 0.0f);
    return out;
}

/// What the shader computes, in one place both sides can be checked against.
///
/// Mirrors `fogFactorHeight` in assets/shaders/fog.glsl exactly, including the
/// clamps - a calibration test that agrees with a different formula from the
/// one running on the GPU is a test of nothing.
inline float heightFogFactor(const HeightFogParams& fog, float cameraZ, float fragZ,
                             float distance) {
    const float density = fog.fogHeight.y;
    const float invScale = fog.fogHeight.z;
    if (density <= 0.0f || invScale <= 0.0f) return 1.0f;

    const float atCamera =
        std::exp(std::clamp(-(cameraZ - fog.fogHeight.x) * invScale, -30.0f, 4.0f));
    const float dz = fragZ - cameraZ;
    float depth;
    if (std::abs(dz) * invScale < 1e-3f) {
        depth = density * atCamera * distance;
    } else {
        const float t = dz * invScale;
        depth = density * distance * atCamera *
                (1.0f - std::exp(std::clamp(-t, -30.0f, 30.0f))) / t;
    }
    return std::clamp(std::exp(-std::max(depth, 0.0f)), 0.0f, 1.0f);
}

/// The linear ramp, for the same reason.
inline float linearFogFactor(float fogStart, float fogEnd, float distance) {
    const float range = fogEnd - fogStart;
    if (range <= 0.0f) return 0.0f;
    return std::clamp((fogEnd - distance) / range, 0.0f, 1.0f);
}

}  // namespace rendering
}  // namespace wowee
