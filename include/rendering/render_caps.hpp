#pragma once

/**
 * render_caps.hpp - what this GPU can be asked to do, decided once.
 *
 * The client's floor is not "2010 hardware" but "hardware with a Vulkan
 * driver", which in practice is Kepler and GCN 1 from 2012
 * (docs/plan-modern-rendering.md §3). Everything above that floor is optional,
 * detected at start-up, and never required: `enable_extension_if_present` is
 * the only spelling in this codebase, and a feature that is missing leaves the
 * frame exactly as it is.
 *
 * That leaves the settings panel with a problem it could not answer before.
 * A row for a technique the GPU cannot do had two bad options - draw it live
 * and let it write a value nothing acts on, or hide it and leave a hole where
 * a player expects a control. `SettingDesc::unavailable` is the third, and
 * this is what fills it in: one struct, filled beside the feature probes in
 * `VkContext::selectPhysicalDevice`, read once at start-up by
 * `applyRenderCapsToSchema()`.
 *
 * Each feature is its own flag rather than only the tier, because the tiers
 * are a summary for the presets and not a fact about any one card: a Turing
 * part without ray query still has variable rate shading, and MoltenVK has
 * neither on hardware that has both.
 */

#include <cstdint>

namespace wowee {
namespace rendering {

/// The coarse summary, for a preset to reason about. Never for a feature test
/// - use the flags below for that.
enum class RenderTier : uint8_t {
    /// Vulkan 1.1, nothing optional. Everything that runs the client today.
    /// Every screen-space and CPU-side technique, and every fallback path.
    Baseline = 0,
    /// descriptorIndexing, bufferDeviceAddress, drawIndirectCount,
    /// multiDrawIndirect and tessellation - Kepler/GCN1 and up, MoltenVK, most
    /// Android. Bindless materials, GPU-driven draws, terrain tessellation.
    Core12 = 1,
    /// Any of fragment shading rate, mesh shaders, ray query or shaderFloat16 -
    /// Turing/RDNA2 and up, and not MoltenVK.
    Modern = 2,
};

/// What the device offered, one flag per thing something might ask for.
///
/// Default-constructed is the floor: every flag false, tier Baseline. A caller
/// that has no VkContext yet - a test, the login screen before the device
/// exists - gets the honest answer rather than an optimistic one.
struct RenderCaps {
    RenderTier tier = RenderTier::Baseline;

    // ---- T1: the Vulkan 1.2 core set ----
    bool descriptorIndexing = false;
    bool bufferDeviceAddress = false;
    bool drawIndirectCount = false;
    bool multiDrawIndirect = false;
    bool tessellation = false;

    // ---- T2: what only 2018-and-later parts have ----
    bool fragmentShadingRate = false;
    bool meshShader = false;
    bool rayQuery = false;
    bool shaderFloat16 = false;

    // ---- T0, but still not everywhere ----
    /// VK_POLYGON_MODE_LINE. Without it the wireframe views cannot be built,
    /// which is `capture_scene --wireframe` and the terrain LOD overlay.
    bool wireframe = false;
    /// textureCompressionBC. Mobile parts have ASTC/ETC2 instead and a DXT BLP
    /// has to be unpacked to RGBA8 first.
    bool blockCompression = false;
    /// Anisotropic filtering.
    bool samplerAnisotropy = false;
    /// Barriers as VkDependencyInfo rather than through the lowering wrapper.
    bool synchronization2 = false;

    /// The largest 2D image this device will make, which bounds the shadow
    /// map's per-cascade side and the normal-map cache's page size. 4096 is
    /// the Vulkan 1.0 guaranteed minimum, so it is the honest default.
    uint32_t maxImage2D = 4096;
    /// How many array layers an image may have. Four cascades need four, and
    /// the guaranteed minimum is 256, so this only ever refuses on something
    /// very unusual - but it is read rather than assumed.
    uint32_t maxImageArrayLayers = 256;

    /// Whether four-layer cascaded shadow maps can be built at all.
    ///
    /// A sampler2DArrayShadow is core Vulkan 1.0, so this is about the image
    /// rather than the shader.
    [[nodiscard]] bool supportsCascadedShadows() const {
        return maxImageArrayLayers >= 4 && maxImage2D >= 512;
    }
};

}  // namespace rendering
}  // namespace wowee
