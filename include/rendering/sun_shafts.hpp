#pragma once

/**
 * sun_shafts.hpp - S4, the light that comes through the trees.
 *
 * Three half-resolution passes and one full-screen add:
 *
 *   1. a blit of the finished frame down to half size, so a shader can read it;
 *   2. `sunshaft_mask.frag` - where the sun is visible, as one `R8` channel;
 *   3. `sunshaft_blur.frag`, twice - the mask smeared along the lines that
 *      leave the sun, 32 taps each, the second continuing where the first left
 *      off;
 *   4. the result added over the frame in the interface pass, before anything
 *      of the interface is drawn.
 *
 * **Where this differs from the phase file.** §S4 places the composite in the
 * existing post pass, after the sky and before FXAA and FSR. It is after them
 * instead, and for a reason that is a property of this renderer rather than a
 * preference: with no upscaler running - which is every default preset, because
 * all four use MSAA and none use FXAA - the scene is drawn multisampled
 * straight into the swapchain image, and the swapchain images are created with
 * `COLOR_ATTACHMENT | TRANSFER_SRC` and no `SAMPLED`. There is no point during
 * the scene pass at which a full-screen shader can read what has been drawn.
 * After the pass ends there is, by a blit, and the interface pass that follows
 * is single-sampled, loads what is already there, and targets the swapchain -
 * which is exactly the pass an additive quad wants.
 *
 * The cost of being downstream of the anti-aliasing is that the shafts are not
 * anti-aliased and not upscaled. They are a wide radial blur of a half
 * resolution mask, so there is nothing in them at the scale either would act
 * on.
 *
 * The mask also reads no depth, which §S4 asks for. Same cause: the scene depth
 * is multisampled on the default path, and a multisampled image can be neither
 * blitted nor sampled by an ordinary `sampler2D`. Brightness stands in for
 * distance - see the header of `sunshaft_mask.frag.glsl`.
 *
 * **Five numbers here were written blind and have now been looked at.** The
 * session that wrote them had no assets to render, so the mask threshold and
 * its ramp (`MaskPush::threshold`, `softness`), the radius the mask fades over,
 * and the two blur passes' decay and weight are the conventional values for
 * this technique rather than values tuned against a picture. They are all in
 * `renderMask()` and in `MaskPush`'s defaults, in one place each.
 *
 * Against two Elwynn cameras they hold: with the sun in open sky above the
 * canopy at 07:00 the composite adds 4.5 of 255 over the whole frame and 12
 * around the sun, taking the fully-white fraction from 0.37 % to 1.71 % - a
 * halo, not a blown highlight; with the sun behind a ridge at 06:30 the added
 * light is rays fanning from the skyline, broken by the treeline. With the sun
 * behind the camera it adds 0.07 of 255 and saturates nothing, because
 * renderMask returns before it blits. The whole thing costs 0.0898 ms of GPU
 * time at 1920x1032. See docs/evidence/phase-01/README.md.
 *
 * Looked at is not tuned. Two cameras in one zone at one weather is a long way
 * from a sweep, and anyone who finds a scene where the halo is too broad should
 * start with the threshold: at 0.72 a bright overcast sky is largely inside the
 * mask, which is what makes the 07:00 shot a glow rather than shafts.
 */

#include <cstdint>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include "rendering/vk_render_target.hpp"
#include "rendering/vk_utils.hpp"

namespace wowee {
namespace rendering {

class VkContext;

class SunShafts {
public:
    SunShafts() = default;
    ~SunShafts() = default;

    SunShafts(const SunShafts&) = delete;
    SunShafts& operator=(const SunShafts&) = delete;

    bool initialize(VkContext* ctx);
    void shutdown();
    /// Targets are half the swapchain, so they move with it.
    void handleSwapchainResize();
    /// The composite pipeline is built against the interface pass, which the
    /// swapchain rebuild remakes.
    void recreatePipelines();

    void setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }
    void setStrength(float strength);
    [[nodiscard]] float getStrength() const { return strength_; }

    /// Steps 1 to 3, outside any render pass.
    ///
    /// @param sceneImage   the swapchain image the frame was drawn into, in
    ///                     `PRESENT_SRC_KHR` - which is where the scene pass
    ///                     leaves it.
    /// @param sunNdc       the sun's place on screen, -1..1.
    /// @param visibility   the lens flare's own answer for whether the sun is
    ///                     there, so the two effects agree.
    /// @return whether anything was produced. False means composite() has
    ///         nothing to add and must not be called.
    bool renderMask(VkCommandBuffer cmd, VkImage sceneImage, VkExtent2D sceneExtent,
                    const glm::vec2& sunNdc, float visibility);

    /// Step 4, inside the interface pass.
    void composite(VkCommandBuffer cmd, const glm::vec3& sunColor, VkExtent2D screenExtent);

private:
    struct MaskPush {
        glm::vec2 sunUV{0.5f};
        float threshold = 0.72f;
        float softness = 0.25f;
        float radius = 0.85f;
        float visibility = 0.0f;
        float aspect = 1.0f;
        float pad = 0.0f;
    };
    struct BlurPush {
        glm::vec2 sunUV{0.5f};
        float density = 0.0f;
        float decay = 0.0f;
        float weight = 0.0f;
        float pad0 = 0.0f, pad1 = 0.0f, pad2 = 0.0f;
    };
    struct CompositePush {
        glm::vec4 sunColorStrength{1.0f};
    };

    bool createTargets();
    void destroyTargets();
    bool createPipelines();
    void destroyPipelines();
    void writeDescriptors();
    void drawFullscreen(VkCommandBuffer cmd, VkPipeline pipeline, VkPipelineLayout layout,
                        VkDescriptorSet set, const void* push, uint32_t pushSize,
                        VkExtent2D extent);

    VkContext* vkCtx_ = nullptr;
    bool enabled_ = true;
    float strength_ = 0.5f;
    /// Whether the three passes ran on the last frame. composite() reads it and
    /// nothing else does: an additive quad over a target that was never written
    /// is whatever the last frame left in it.
    bool producedLastFrame_ = false;

    uint32_t width_ = 0;
    uint32_t height_ = 0;

    /// The frame at half size, as something a shader can read.
    AllocatedImage sceneCopy_{};
    VkSampler sampler_ = VK_NULL_HANDLE;  ///< owned by the context's cache

    VkRenderTarget maskTarget_;
    VkRenderTarget blurTarget_;

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descPool_ = VK_NULL_HANDLE;
    VkDescriptorSet sceneSet_ = VK_NULL_HANDLE;  ///< reads sceneCopy_
    VkDescriptorSet maskSet_ = VK_NULL_HANDLE;   ///< reads maskTarget_
    VkDescriptorSet blurSet_ = VK_NULL_HANDLE;   ///< reads blurTarget_

    VkPipelineLayout maskLayout_ = VK_NULL_HANDLE;
    VkPipeline maskPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout blurLayout_ = VK_NULL_HANDLE;
    VkPipeline blurPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout compositeLayout_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
};

}  // namespace rendering
}  // namespace wowee
