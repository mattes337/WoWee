#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <atomic>
#include <chrono>

namespace wowee {
namespace rendering {

static constexpr uint32_t MAX_LOCAL_LIGHTS = 64;

// Must match the PerFrame UBO layout in all shaders (std140 alignment)
struct GPUPerFrameData {
    glm::mat4 view;
    glm::mat4 projection;
    glm::mat4 lightSpaceMatrix;
    glm::vec4 lightDir;       // xyz = direction, w = unused
    glm::vec4 lightColor;     // xyz = color, w = unused
    glm::vec4 ambientColor;   // xyz = color, w = unused
    glm::vec4 viewPos;        // xyz = camera pos, w = unused
    glm::vec4 fogColor;       // xyz = color, w = unused
    glm::vec4 fogParams;      // x = fogStart, y = fogEnd, z = time, w = water ripple strength
    glm::vec4 shadowParams;   // x = enabled(0/1), y = strength, z = one shadow-map texel, w = unused
    // The player, for effects that react to where they are standing: water
    // ripples and the foliage the player brushes past. playerWake trails the
    // player by a fixed time constant, so clutter the player has already walked
    // through springs back over that interval instead of snapping upright.
    glm::vec4 playerPos;      // xyz = player world position, w = horizontal speed (yd/s)
    glm::vec4 playerWake;     // xyz = trailing player position, w = unused
    glm::vec4 localLightPosRadius[MAX_LOCAL_LIGHTS];       // xyz = position, w = radius
    glm::vec4 localLightColorIntensity[MAX_LOCAL_LIGHTS];  // rgb = color, w = intensity
    glm::ivec4 localLightMeta;                             // x = active light count

    // ---- appended in phase 01, all at once ----
    //
    // This block is mirrored by hand in every shader that declares it, so it
    // grows once per phase rather than once per technique: five techniques
    // adding a vec4 each is five chances to get an offset wrong in one of the
    // copies, and std140 gives no error for that - it gives a shader reading
    // the field next to the one it meant.
    //
    // A shader declares a *prefix* of this block, never a hole in it, so a
    // shader that reads none of these does not mention them. That is what the
    // client already did: skybox.frag stops at shadowParams.

    /// RESERVED(phase-01b, L1-csm): the four cascade view-projections. Phase 01
    /// shipped the consolidation and left cascaded shadows to 01b (see
    /// docs/modern-rendering/01-shadows-fog-distance-surfaces.md, "Cut order"),
    /// but the slots are here so this block does not move again when they land.
    /// cascadeMatrix[0] is kept equal to lightSpaceMatrix; the rest are zero
    /// and no shader reads them.
    glm::mat4 cascadeMatrix[4] = {};
    /// RESERVED(phase-01b, L1-csm): view-space depth at which each cascade ends.
    glm::vec4 shadowSplits{0.0f};
    /// RESERVED(phase-01b, L1-csm): x = cascade count, y = blend band in yards,
    /// z = filter, w = unused.
    glm::ivec4 shadowMeta{1, 0, 0, 0};

    /// Height fog: x = base height in world Z, y = density per yard at that
    /// height, z = 1 / scale height, w = aerial-perspective strength.
    ///
    /// RESERVED(phase-15, A2-volumetric-fog): these and fogSunColor below are
    /// also the froxel volume's inputs; declared with the fog parameters so
    /// this block moves once rather than twice.
    glm::vec4 fogHeight{0.0f};
    /// The colour the sun scatters into the fog: rgb = colour, w unused. Taken
    /// from the zone's own Light.dbc directional colour, unchanged, so nothing
    /// authored shifts hue. How much of it reaches the frame is fogHeight.w.
    glm::vec4 fogSunColor{0.0f};

    /// RESERVED(phase-11, L5-sky-probes): SH9 ambient slots appended now so
    /// PerFrame moves once. Zero-filled; no shader reads them.
    glm::vec4 skySH[7] = {};
};

// Push constants for the model matrix (most common case)
struct GPUPushConstants {
    glm::mat4 model;
};

/// The WMO's own, which carries where a batch of cloth hangs beside the model
/// matrix: (top z, drop, centre x, centre y) in the model's local space.
struct WMOPushConstants {
    glm::mat4 model;
    glm::vec4 cloth{0.0f};
};

// Push constants for shadow rendering passes
struct ShadowPush {
    glm::mat4 lightSpaceMatrix;
    glm::mat4 model;
};

// Uniform buffer for shadow rendering parameters (matches shader std140 layout)
struct ShadowParamsUBO {
    int32_t useBones;
    int32_t useTexture;
    int32_t alphaTest;
    int32_t foliageSway;
    float windTime;
    float foliageMotionDamp;
};

// Timer utility for performance profiling queries.
// Uses atomics because floor-height queries are dispatched on async threads
// from CameraController while the main thread may read the counters.
struct QueryTimer {
    std::atomic<double>* totalMs = nullptr;
    std::atomic<uint32_t>* callCount = nullptr;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    QueryTimer(std::atomic<double>* total, std::atomic<uint32_t>* calls)
        : totalMs(total), callCount(calls) {}
    ~QueryTimer() {
        if (callCount) {
            callCount->fetch_add(1, std::memory_order_relaxed);
        }
        if (totalMs) {
            auto end = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            // Relaxed is fine for diagnostics - exact ordering doesn't matter.
            double old = totalMs->load(std::memory_order_relaxed);
            while (!totalMs->compare_exchange_weak(old, old + ms, std::memory_order_relaxed)) {}
        }
    }
};

} // namespace rendering
} // namespace wowee
