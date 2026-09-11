#include "rendering/sun_shafts.hpp"

#include <algorithm>
#include <array>

#include "core/logger.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_shader.hpp"

namespace wowee {
namespace rendering {

namespace {
/// The mask and both blurs run at half the swapchain on each axis - a quarter
/// of the pixels - because everything they compute is a wide blur of a soft
/// threshold and there is nothing in the result at a higher frequency than
/// that.
constexpr uint32_t kDownscale = 2;
}  // namespace

bool SunShafts::initialize(VkContext* ctx) {
    vkCtx_ = ctx;
    if (!vkCtx_) return false;

    VkSamplerCreateInfo samplerInfo{.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_ = vkCtx_->getOrCreateSampler(samplerInfo);
    if (sampler_ == VK_NULL_HANDLE) {
        LOG_ERROR("SunShafts: no sampler");
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayout_ = createDescriptorSetLayout(vkCtx_->getDevice(), {binding});
    if (!setLayout_) {
        LOG_ERROR("SunShafts: failed to create descriptor set layout");
        return false;
    }

    VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  .descriptorCount = 3};
    VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 3;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(vkCtx_->getDevice(), &poolInfo, nullptr, &descPool_) != VK_SUCCESS) {
        LOG_ERROR("SunShafts: failed to create descriptor pool");
        return false;
    }

    std::array<VkDescriptorSetLayout, 3> layouts = {setLayout_, setLayout_, setLayout_};
    VkDescriptorSetAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool_;
    ai.descriptorSetCount = 3;
    ai.pSetLayouts = layouts.data();
    std::array<VkDescriptorSet, 3> sets{};
    if (vkAllocateDescriptorSets(vkCtx_->getDevice(), &ai, sets.data()) != VK_SUCCESS) {
        LOG_ERROR("SunShafts: failed to allocate descriptor sets");
        return false;
    }
    sceneSet_ = sets[0];
    maskSet_ = sets[1];
    blurSet_ = sets[2];

    if (!createTargets()) return false;
    if (!createPipelines()) return false;
    writeDescriptors();
    LOG_INFO("Sun shafts initialized at ", width_, "x", height_);
    return true;
}

bool SunShafts::createTargets() {
    const VkExtent2D ext = vkCtx_->getSwapchainExtent();
    width_ = std::max(1u, ext.width / kDownscale);
    height_ = std::max(1u, ext.height / kDownscale);

    sceneCopy_ = createImage(vkCtx_->getDevice(), vkCtx_->getAllocator(), width_, height_,
                             VK_FORMAT_R8G8B8A8_UNORM,
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    if (!sceneCopy_.image) {
        LOG_ERROR("SunShafts: failed to create the scene copy");
        return false;
    }

    // R8: the mask is one number per pixel and never anything else.
    if (!maskTarget_.create(*vkCtx_, width_, height_, VK_FORMAT_R8_UNORM) ||
        !blurTarget_.create(*vkCtx_, width_, height_, VK_FORMAT_R8_UNORM)) {
        LOG_ERROR("SunShafts: failed to create the half-resolution targets");
        return false;
    }
    return true;
}

void SunShafts::destroyTargets() {
    if (!vkCtx_) return;
    VkDevice device = vkCtx_->getDevice();
    VmaAllocator alloc = vkCtx_->getAllocator();
    maskTarget_.destroy(device, alloc);
    blurTarget_.destroy(device, alloc);
    destroyImage(device, alloc, sceneCopy_);
}

bool SunShafts::createPipelines() {
    VkDevice device = vkCtx_->getDevice();

    VkShaderModule vert;
    if (!vert.loadFromFile(device, "assets/shaders/postprocess.vert.spv")) {
        LOG_ERROR("SunShafts: failed to load the fullscreen vertex shader");
        return false;
    }

    struct PassSpec {
        const char* frag;
        uint32_t pushSize;
        VkRenderPass pass;
        VkPipelineColorBlendAttachmentState blend;
        VkPipelineLayout* layout;
        VkPipeline* pipeline;
    };
    const std::array<PassSpec, 3> specs = {{
        {"assets/shaders/sunshaft_mask.frag.spv", sizeof(MaskPush),
         maskTarget_.getRenderPass(), PipelineBuilder::blendDisabled(), &maskLayout_,
         &maskPipeline_},
        {"assets/shaders/sunshaft_blur.frag.spv", sizeof(BlurPush),
         maskTarget_.getRenderPass(), PipelineBuilder::blendDisabled(), &blurLayout_,
         &blurPipeline_},
        // The interface pass: single-sampled, colour only, and it loads what the
        // scene left rather than clearing it - which is what an additive quad
        // over the finished frame needs.
        {"assets/shaders/sunshaft_composite.frag.spv", sizeof(CompositePush),
         vkCtx_->getOverlayRenderPass(), PipelineBuilder::blendAdditive(), &compositeLayout_,
         &compositePipeline_},
    }};

    bool ok = true;
    for (const auto& spec : specs) {
        VkShaderModule frag;
        if (!frag.loadFromFile(device, spec.frag)) {
            LOG_ERROR("SunShafts: failed to load ", spec.frag);
            ok = false;
            break;
        }

        VkPushConstantRange range{};
        range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        range.offset = 0;
        range.size = spec.pushSize;
        VkPipelineLayoutCreateInfo lci{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lci.setLayoutCount = 1;
        lci.pSetLayouts = &setLayout_;
        lci.pushConstantRangeCount = 1;
        lci.pPushConstantRanges = &range;
        if (vkCreatePipelineLayout(device, &lci, nullptr, spec.layout) != VK_SUCCESS) {
            LOG_ERROR("SunShafts: failed to create a pipeline layout");
            frag.destroy();
            ok = false;
            break;
        }

        *spec.pipeline = PipelineBuilder()
                             .setShaders(vert.stageInfo(VK_SHADER_STAGE_VERTEX_BIT),
                                         frag.stageInfo(VK_SHADER_STAGE_FRAGMENT_BIT))
                             .setVertexInput({}, {})
                             .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
                             .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
                             .setNoDepthTest()
                             .setColorBlendAttachment(spec.blend)
                             .setMultisample(VK_SAMPLE_COUNT_1_BIT)
                             .setLayout(*spec.layout)
                             .setRenderPass(spec.pass)
                             .setDynamicStates(viewportAndScissorDynamic())
                             .build(device, vkCtx_->getPipelineCache());
        frag.destroy();
        if (*spec.pipeline == VK_NULL_HANDLE) {
            LOG_ERROR("SunShafts: failed to build a pipeline for ", spec.frag);
            ok = false;
            break;
        }
    }
    vert.destroy();
    return ok;
}

void SunShafts::destroyPipelines() {
    if (!vkCtx_) return;
    VkDevice device = vkCtx_->getDevice();
    destroy(device, maskPipeline_);
    destroy(device, maskLayout_);
    destroy(device, blurPipeline_);
    destroy(device, blurLayout_);
    destroy(device, compositePipeline_);
    destroy(device, compositeLayout_);
}

void SunShafts::writeDescriptors() {
    const std::array<std::pair<VkDescriptorSet, VkImageView>, 3> pairs = {{
        {sceneSet_, sceneCopy_.imageView},
        {maskSet_, maskTarget_.getColorImageView()},
        {blurSet_, blurTarget_.getColorImageView()},
    }};

    std::array<VkDescriptorImageInfo, 3> infos{};
    std::array<VkWriteDescriptorSet, 3> writes{};
    for (size_t i = 0; i < pairs.size(); ++i) {
        infos[i].sampler = sampler_;
        infos[i].imageView = pairs[i].second;
        infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = pairs[i].first;
        writes[i].dstBinding = 0;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(vkCtx_->getDevice(), 3, writes.data(), 0, nullptr);
}

void SunShafts::shutdown() {
    if (!vkCtx_) return;
    VkDevice device = vkCtx_->getDevice();
    destroyPipelines();
    destroyTargets();
    if (descPool_) {
        vkDestroyDescriptorPool(device, descPool_, nullptr);
        descPool_ = VK_NULL_HANDLE;
        sceneSet_ = maskSet_ = blurSet_ = VK_NULL_HANDLE;
    }
    destroy(device, setLayout_);
    sampler_ = VK_NULL_HANDLE;  // owned by the context's sampler cache
    vkCtx_ = nullptr;
}

void SunShafts::handleSwapchainResize() {
    if (!vkCtx_) return;
    destroyTargets();
    if (!createTargets()) {
        LOG_ERROR("SunShafts: the resized targets could not be made; shafts are off");
        enabled_ = false;
        return;
    }
    writeDescriptors();
}

void SunShafts::recreatePipelines() {
    if (!vkCtx_) return;
    destroyPipelines();
    if (!createPipelines()) {
        LOG_ERROR("SunShafts: the pipelines could not be rebuilt; shafts are off");
        enabled_ = false;
    }
}

void SunShafts::setStrength(float strength) {
    strength_ = std::clamp(strength, 0.0f, 1.0f);
}

void SunShafts::drawFullscreen(VkCommandBuffer cmd, VkPipeline pipeline,
                               VkPipelineLayout layout, VkDescriptorSet set,
                               const void* push, uint32_t pushSize, VkExtent2D extent) {
    VkViewport vp{};
    vp.width = static_cast<float>(extent.width);
    vp.height = static_cast<float>(extent.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc{};
    sc.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &sc);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, push);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

bool SunShafts::renderMask(VkCommandBuffer cmd, VkImage sceneImage, VkExtent2D sceneExtent,
                           const glm::vec2& sunNdc, float visibility) {
    producedLastFrame_ = false;
    if (!enabled_ || !vkCtx_ || cmd == VK_NULL_HANDLE) return false;
    if (maskPipeline_ == VK_NULL_HANDLE || sceneImage == VK_NULL_HANDLE) return false;
    if (visibility < 0.01f || strength_ <= 0.001f) return false;
    // Off screen by a wide margin. The mask fades with distance from the sun
    // anyway, but there is no reason to blit a frame for a shaft nobody sees.
    if (std::abs(sunNdc.x) > 1.6f || std::abs(sunNdc.y) > 1.6f) return false;
    if (sceneExtent.width == 0 || sceneExtent.height == 0) return false;

    // ---- 1. the frame, down to half size and into something sampleable ----
    //
    // The scene pass leaves the swapchain image in PRESENT_SRC_KHR, which is
    // where the interface pass expects to find it, so it goes there and back.
    transitionImageLayout(cmd, sceneImage, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);
    transitionImageLayout(cmd, sceneCopy_.image, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = 0,
                           .baseArrayLayer = 0,
                           .layerCount = 1};
    blit.srcOffsets[1] = {static_cast<int32_t>(sceneExtent.width),
                          static_cast<int32_t>(sceneExtent.height), 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.dstOffsets[1] = {static_cast<int32_t>(width_), static_cast<int32_t>(height_), 1};
    vkCmdBlitImage(cmd, sceneImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sceneCopy_.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

    transitionImageLayout(cmd, sceneImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
    transitionImageLayout(cmd, sceneCopy_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_PIPELINE_STAGE_TRANSFER_BIT,
                          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    const VkExtent2D half{width_, height_};
    const glm::vec2 sunUV = sunNdc * 0.5f + 0.5f;

    // ---- 2. the mask ----
    MaskPush maskPush{};
    maskPush.sunUV = sunUV;
    maskPush.visibility = visibility;
    maskPush.aspect = static_cast<float>(sceneExtent.width) /
                      static_cast<float>(std::max(1u, sceneExtent.height));
    maskTarget_.beginPass(cmd, {{0.0f, 0.0f, 0.0f, 0.0f}});
    drawFullscreen(cmd, maskPipeline_, maskLayout_, sceneSet_, &maskPush, sizeof(maskPush), half);
    maskTarget_.endPass(cmd);

    // ---- 3. two radial blurs, mask -> blur -> mask ----
    //
    // The first walks a thirty-second of the way to the sun in thirty-two
    // steps; the second takes the whole distance in thirty-two steps of what
    // the first already summed, which is sixty-four taps' reach for
    // thirty-two's cost. The weights are what keeps the two from doubling the
    // brightness between them.
    BlurPush pass1{};
    pass1.sunUV = sunUV;
    pass1.density = 1.0f / 32.0f;
    pass1.decay = 0.96f;
    pass1.weight = 1.0f / 32.0f;
    blurTarget_.beginPass(cmd, {{0.0f, 0.0f, 0.0f, 0.0f}});
    drawFullscreen(cmd, blurPipeline_, blurLayout_, maskSet_, &pass1, sizeof(pass1), half);
    blurTarget_.endPass(cmd);

    BlurPush pass2 = pass1;
    pass2.density = 1.0f;
    pass2.decay = 0.94f;
    pass2.weight = 1.0f / 16.0f;
    maskTarget_.beginPass(cmd, {{0.0f, 0.0f, 0.0f, 0.0f}});
    drawFullscreen(cmd, blurPipeline_, blurLayout_, blurSet_, &pass2, sizeof(pass2), half);
    maskTarget_.endPass(cmd);

    producedLastFrame_ = true;
    return true;
}

void SunShafts::composite(VkCommandBuffer cmd, const glm::vec3& sunColor,
                          VkExtent2D screenExtent) {
    if (!producedLastFrame_ || compositePipeline_ == VK_NULL_HANDLE) return;
    CompositePush push{};
    push.sunColorStrength = glm::vec4(sunColor, strength_);
    drawFullscreen(cmd, compositePipeline_, compositeLayout_, maskSet_, &push, sizeof(push),
                   screenExtent);
}

}  // namespace rendering
}  // namespace wowee
