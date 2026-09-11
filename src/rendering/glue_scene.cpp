#include "rendering/glue_scene.hpp"

#include "core/logger.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_asset_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "rendering/imgui_texture.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_render_target.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/vk_utils.hpp"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace wowee::rendering {

namespace {

/// One model at a time, so the id is a constant rather than a counter: a
/// scene shows one model and swapping scenes replaces it.
constexpr uint32_t kSceneModelId = 9995;

/// Point the viewport and scissor at the top-left w x h of whatever pass is
/// open, which is the part of the target that carries the picture.
void setUsedViewport(VkCommandBuffer cmd, int w, int h) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(std::max(1, w)),
                  static_cast<float>(std::max(1, h)), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {static_cast<uint32_t>(std::max(1, w)),
                         static_cast<uint32_t>(std::max(1, h))}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

/// The .m2 a glue screen's model reference means. .mdx means .m2, the same
/// way .tga means .blp elsewhere in this interface: Blizzard's markup still
/// names models by the extension the format carried before 3.x and no .mdx
/// has shipped in the archives since; the original client takes the name and
/// loads the .m2 beside it. AccountLogin_OnLoad is the one that matters -
/// SetModel("...UI_MainMenu_Northrend.mdx") is the only statement of what the
/// login screen looks like.
std::string sceneModelPath(const std::string& rawPath) {
    std::string m2Path = rawPath;
    if (m2Path.size() > 4) {
        std::string tail = m2Path.substr(m2Path.size() - 4);
        for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".mdx") m2Path.replace(m2Path.size() - 4, 4, ".m2");
    }
    return m2Path;
}

} // namespace

/// Everything the scene needs a Vulkan device for.
struct GlueScene::View {
    pipeline::AssetManager* assets = nullptr;
    VkContext* ctx = nullptr;

    std::unique_ptr<M2Renderer> models;
    /// The loaded model, kept because M2Renderer hands back no view of what
    /// it uploaded and frame() needs the cameras, stage() the attachments and
    /// record() the lights.
    pipeline::M2Model modelData;
    std::unique_ptr<VkRenderTarget> target;

    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkBuffer ubo[kSlots] = {};
    VmaAllocation uboAlloc[kSlots] = {};
    void* uboMapped[kSlots] = {};
    VkDescriptorSet perFrameSet[kSlots] = {};

    // The renderer's per-frame set declares a shadow map at binding 1. There
    // is no shadow pass here, so it is a 1x1 depth image cleared to "nothing
    // in the way".
    VkImage shadowImage = VK_NULL_HANDLE;
    VkImageView shadowView = VK_NULL_HANDLE;
    VmaAllocation shadowAlloc = VK_NULL_HANDLE;

    VkDescriptorSet imguiTexture = VK_NULL_HANDLE;

    // The screen's glow.
    //
    // ModelFFX carries a glow figure on five of the glue screens and the model
    // shader has no glow term. It is done here, after the scene is drawn, the
    // way a bloom is normally done: take what is bright, blur it, and add it
    // back. Half resolution, because a blur is what this is and the extra
    // detail would only be thrown away.
    std::unique_ptr<VkRenderTarget> bloomTarget;
    VkDescriptorSetLayout bloomSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet bloomSceneSet = VK_NULL_HANDLE;   // samples the scene
    VkPipelineLayout bloomPipelineLayout = VK_NULL_HANDLE;
    VkPipeline bloomPipeline = VK_NULL_HANDLE;

    // And the pass that adds it back. Full size, because its result is the
    // picture: the interface draws this image rather than the scene whenever
    // the screen asked for a glow. It has to be a pass of its own - an ImGui
    // draw list has one blend state, and an alpha blend cannot add.
    std::unique_ptr<VkRenderTarget> glowTarget;
    VkDescriptorSetLayout glowSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet glowInputSet = VK_NULL_HANDLE;    // samples the scene and the bloom
    VkPipelineLayout glowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline glowPipeline = VK_NULL_HANDLE;
    VkDescriptorSet glowTextureId = VK_NULL_HANDLE;   // what the interface draws
    /// Set by the pass, cleared when it does not run. The owner reads the
    /// scene straight when this is false, so a screen with no glow - or a run
    /// with WOWEE_NO_GLUE_GLOW - costs nothing and shows nothing.
    bool glowApplied = false;
    float glow = 0.0f;

    int width = 0;
    int height = 0;
    int drawWidth = 0;
    int drawHeight = 0;
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 1.0f};

    [[nodiscard]] int usedWidth() const { return drawWidth > 0 ? drawWidth : width; }
    [[nodiscard]] int usedHeight() const { return drawHeight > 0 ? drawHeight : height; }
    [[nodiscard]] float drawAspect() const {
        const int h = usedHeight();
        return h > 0 ? static_cast<float>(usedWidth()) / static_cast<float>(h) : 1.0f;
    }
    [[nodiscard]] float usedU() const {
        return width > 0 ? static_cast<float>(usedWidth()) / static_cast<float>(width) : 1.0f;
    }
    [[nodiscard]] float usedV() const {
        return height > 0 ? static_cast<float>(usedHeight()) / static_cast<float>(height) : 1.0f;
    }

    /// The path last handed to loadScene, whether or not it loaded - see the
    /// note in GlueScene::show.
    std::string loadedPath;
    /// Set when the loaded model was placed by its own camera. A scene that
    /// carries none is left undrawn rather than framed by a guess.
    bool placed = false;
    /// Seconds the scene has been updated for, written into the per-frame
    /// block for the vertex shader's time-driven effects.
    float sceneTime = 0.0f;
    bool everRecorded = false;
    uint32_t instanceId = 0;

    /// What the model on show was last built from, so a screen that says the
    /// same thing every frame - and they all do, because the client reads it
    /// back rather than being told when it changes - reloads nothing.
    int cameraIndex = 0;
    int appliedSequence = -1;
    float appliedScale = 1.0f;
    /// The fog and lights, written into the per-frame block on every record.
    /// Cheap enough that there is nothing to compare against.
    GlueSceneFogRange fog{9999.0f, 10000.0f};
    glm::vec3 fogColor{0.0f, 0.0f, 0.0f};
    GlueSceneLighting lighting;

    bool build(int w, int h, Renderer* renderer);
    bool buildBloom(int w, int h);
    void destroy();
    bool loadScene(const std::string& rawPath, const GlueSceneState& scene);
    void applyScene(const GlueSceneState& scene);
    void clearScene();
    void writePerFrame(uint32_t slot, const Camera& camera);
    void record(VkCommandBuffer cmd, uint32_t slot, const Camera& camera,
                const FigureDraw& figures);
    void renderGlow(VkCommandBuffer cmd);
};

bool GlueScene::View::build(int w, int h, Renderer* renderer) {
    ctx = renderer->getVkContext();
    const VkDescriptorSetLayout perFrameLayout = renderer->getPerFrameSetLayout();
    if (!ctx || perFrameLayout == VK_NULL_HANDLE) return false;

    width = w;
    height = h;

    VkDevice device = ctx->getDevice();
    VmaAllocator allocator = ctx->getAllocator();

    target = std::make_unique<VkRenderTarget>();
    if (!target->create(*ctx, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                        VK_FORMAT_R8G8B8A8_UNORM, true, VK_SAMPLE_COUNT_4_BIT)) {
        LOG_WARNING("GlueScene: could not create the ", width, "x", height, " view");
        target.reset();
        return false;
    }

    // The owner may hand the image to the interface on the frame before the
    // first pass has run. Put it in the layout ImGui samples from now, so that
    // frame reads a black image rather than one in UNDEFINED layout.
    ctx->immediateSubmit([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier2 toRead{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        toRead.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        toRead.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        toRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toRead.image = target->getColorImage();
        toRead.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                   .baseMipLevel = 0, .levelCount = 1,
                                   .baseArrayLayer = 0, .layerCount = 1};
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkDependencyInfo dep{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &toRead;
        cmdPipelineBarrier2(cmd, dep);
    });

    models = std::make_unique<M2Renderer>();
    // Before initialize, as setSceneMode wants: this is one authored scene,
    // not a field of world doodads, so its particles are not damped.
    models->setSceneMode(true);
    if (!models->initialize(ctx, perFrameLayout, assets, target->getRenderPass(),
                            target->getSampleCount())) {
        LOG_WARNING("GlueScene: could not build the model renderer");
        return false;
    }

    // --- the shadow-map stand-in ---
    {
        VkImageCreateInfo imgCI{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imgCI.imageType = VK_IMAGE_TYPE_2D;
        imgCI.format = VK_FORMAT_D16_UNORM;
        imgCI.extent = {.width = 1, .height = 1, .depth = 1};
        imgCI.mipLevels = 1;
        imgCI.arrayLayers = 1;
        imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
        imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocCI{};
        allocCI.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        if (vmaCreateImage(allocator, &imgCI, &allocCI, &shadowImage, &shadowAlloc,
                           nullptr) != VK_SUCCESS) {
            LOG_WARNING("GlueScene: could not create the shadow stand-in");
            return false;
        }
        VkImageViewCreateInfo viewCI{.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewCI.image = shadowImage;
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = VK_FORMAT_D16_UNORM;
        viewCI.subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                   .baseMipLevel = 0, .levelCount = 1,
                                   .baseArrayLayer = 0, .layerCount = 1};
        if (vkCreateImageView(device, &viewCI, nullptr, &shadowView) != VK_SUCCESS) {
            LOG_WARNING("GlueScene: could not create the shadow stand-in view");
            return false;
        }
        ctx->immediateSubmit([&](VkCommandBuffer cmd) {
            const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                                .baseMipLevel = 0, .levelCount = 1,
                                                .baseArrayLayer = 0, .layerCount = 1};
            VkImageMemoryBarrier2 toTransfer{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toTransfer.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            toTransfer.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toTransfer.image = shadowImage;
            toTransfer.subresourceRange = range;
            toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            VkDependencyInfo depA{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depA.imageMemoryBarrierCount = 1;
            depA.pImageMemoryBarriers = &toTransfer;
            cmdPipelineBarrier2(cmd, depA);

            const VkClearDepthStencilValue clearVal{.depth = 1.0f, .stencil = 0};
            vkCmdClearDepthStencilImage(cmd, shadowImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        &clearVal, 1, &range);

            VkImageMemoryBarrier2 toRead{.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toRead.srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
            toRead.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toRead.image = shadowImage;
            toRead.subresourceRange = range;
            toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            VkDependencyInfo depB{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            depB.imageMemoryBarrierCount = 1;
            depB.pImageMemoryBarriers = &toRead;
            cmdPipelineBarrier2(cmd, depB);
        });
    }

    // --- the per-frame sets the model pipelines read their matrices from ---
    {
        // kSlots per-frame sets, plus the bloom pass's one scene sampler and
        // the glow pass's scene-and-bloom pair.
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = kSlots;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = kSlots + 3;
        VkDescriptorPoolCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.maxSets = kSlots + 2;
        ci.poolSizeCount = 2;
        ci.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(device, &ci, nullptr, &descPool) != VK_SUCCESS) {
            LOG_WARNING("GlueScene: could not create the descriptor pool");
            return false;
        }

        for (uint32_t slot = 0; slot < kSlots; ++slot) {
            VkBufferCreateInfo bufInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            bufInfo.size = sizeof(GPUPerFrameData);
            bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
            allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo mapInfo{};
            if (vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &ubo[slot], &uboAlloc[slot],
                                &mapInfo) != VK_SUCCESS) {
                LOG_WARNING("GlueScene: could not create the per-frame buffer");
                return false;
            }
            uboMapped[slot] = mapInfo.pMappedData;

            VkDescriptorSetAllocateInfo setAlloc{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            setAlloc.descriptorPool = descPool;
            setAlloc.descriptorSetCount = 1;
            setAlloc.pSetLayouts = &perFrameLayout;
            if (vkAllocateDescriptorSets(device, &setAlloc, &perFrameSet[slot]) != VK_SUCCESS) {
                LOG_WARNING("GlueScene: could not allocate the per-frame set");
                return false;
            }

            VkDescriptorBufferInfo descBuf{};
            descBuf.buffer = ubo[slot];
            descBuf.offset = 0;
            descBuf.range = sizeof(GPUPerFrameData);
            VkDescriptorImageInfo shadowImg{};
            // The sampler is ignored: binding 1 of the renderer's layout
            // declares an immutable comparison sampler of its own.
            shadowImg.imageView = shadowView;
            shadowImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writes[2]{};
            writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[0].dstSet = perFrameSet[slot];
            writes[0].dstBinding = 0;
            writes[0].descriptorCount = 1;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo = &descBuf;
            writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[1].dstSet = perFrameSet[slot];
            writes[1].dstBinding = 1;
            writes[1].descriptorCount = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[1].pImageInfo = &shadowImg;
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
        }
    }

    imguiTexture = ImGui_ImplVulkan_AddTexture(target->getSampler(),
                                               target->getColorImageView(),
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!buildBloom(width, height)) {
        // The scene still draws; only its glow is missing. Said once, because
        // a glow of 0.08 going quietly absent is exactly the kind of thing
        // that gets rediscovered from a screenshot months later.
        LOG_WARNING("GlueScene: no glow pass - the scene draws without it");
    }

    LOG_INFO("GlueScene: view built (", width, "x", height, ")");
    return true;
}

/// The half-size target, pipeline and descriptor the glow pass needs.
///
/// Half size on purpose: the pass blurs, and detail thrown into a blur is
/// detail paid for and discarded. It also halves the taps' cost, which is the
/// whole of what this costs per frame.
bool GlueScene::View::buildBloom(int w, int h) {
    VkDevice device = ctx->getDevice();

    bloomTarget = std::make_unique<VkRenderTarget>();
    const uint32_t bw = static_cast<uint32_t>(std::max(1, w / 2));
    const uint32_t bh = static_cast<uint32_t>(std::max(1, h / 2));
    if (!bloomTarget->create(*ctx, bw, bh, VK_FORMAT_R8G8B8A8_UNORM, false,
                             VK_SAMPLE_COUNT_1_BIT)) {
        bloomTarget.reset();
        return false;
    }

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo lci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    lci.bindingCount = 1;
    lci.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(device, &lci, nullptr, &bloomSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4};
    VkPipelineLayoutCreateInfo plci{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &bloomSetLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pcr;
    if (vkCreatePipelineLayout(device, &plci, nullptr, &bloomPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkShaderModule vert, frag;
    if (!vert.loadFromFile(device, "assets/shaders/glue_bloom.vert.spv") ||
        !frag.loadFromFile(device, "assets/shaders/glue_bloom.frag.spv")) {
        return false;
    }

    VkPipelineShaderStageCreateInfo vs{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    vs.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vs.module = vert.getModule();
    vs.pName = "main";
    VkPipelineShaderStageCreateInfo fs{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    fs.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fs.module = frag.getModule();
    fs.pName = "main";

    PipelineBuilder builder;
    bloomPipeline = builder
        .setShaders(vs, fs)
        .setVertexInput({}, {})          // the triangle comes from gl_VertexIndex
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest()
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(VK_SAMPLE_COUNT_1_BIT)
        .setLayout(bloomPipelineLayout)
        .setRenderPass(bloomTarget->getRenderPass())
        .setDynamicStates({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
        .build(device);
    if (bloomPipeline == VK_NULL_HANDLE) return false;

    VkDescriptorSetAllocateInfo ai{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &bloomSetLayout;
    if (vkAllocateDescriptorSets(device, &ai, &bloomSceneSet) != VK_SUCCESS) return false;

    const VkDescriptorImageInfo sceneInfo = target->descriptorInfo();
    VkWriteDescriptorSet write{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = bloomSceneSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &sceneInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    // --- and the pass that adds the bloom back to the scene ---
    //
    // Full size and no blend: it writes the finished picture, which is what
    // the interface samples. The blur above stays at half size because a blur
    // throws detail away; this one cannot, because it carries the scene.
    glowTarget = std::make_unique<VkRenderTarget>();
    if (!glowTarget->create(*ctx, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                            VK_FORMAT_R8G8B8A8_UNORM, false, VK_SAMPLE_COUNT_1_BIT)) {
        glowTarget.reset();
        return false;
    }

    VkDescriptorSetLayoutBinding glowBindings[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        glowBindings[i].binding = i;
        glowBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        glowBindings[i].descriptorCount = 1;
        glowBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo glci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    glci.bindingCount = 2;
    glci.pBindings = glowBindings;
    if (vkCreateDescriptorSetLayout(device, &glci, nullptr, &glowSetLayout) != VK_SUCCESS) {
        return false;
    }

    VkPushConstantRange gpcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 3};
    VkPipelineLayoutCreateInfo gplci{.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    gplci.setLayoutCount = 1;
    gplci.pSetLayouts = &glowSetLayout;
    gplci.pushConstantRangeCount = 1;
    gplci.pPushConstantRanges = &gpcr;
    if (vkCreatePipelineLayout(device, &gplci, nullptr, &glowPipelineLayout) != VK_SUCCESS) {
        return false;
    }

    VkShaderModule glowFrag;
    if (!glowFrag.loadFromFile(device, "assets/shaders/glue_glow.frag.spv")) return false;
    VkPipelineShaderStageCreateInfo gfs{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    gfs.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    gfs.module = glowFrag.getModule();
    gfs.pName = "main";

    PipelineBuilder glowBuilder;
    glowPipeline = glowBuilder
        .setShaders(vs, gfs)             // the same fullscreen-triangle vertex stage
        .setVertexInput({}, {})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest()
        .setColorBlendAttachment(PipelineBuilder::blendDisabled())
        .setMultisample(VK_SAMPLE_COUNT_1_BIT)
        .setLayout(glowPipelineLayout)
        .setRenderPass(glowTarget->getRenderPass())
        .setDynamicStates({VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
        .build(device);
    if (glowPipeline == VK_NULL_HANDLE) return false;

    VkDescriptorSetAllocateInfo gai{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    gai.descriptorPool = descPool;
    gai.descriptorSetCount = 1;
    gai.pSetLayouts = &glowSetLayout;
    if (vkAllocateDescriptorSets(device, &gai, &glowInputSet) != VK_SUCCESS) return false;

    const VkDescriptorImageInfo glowInputs[2]{target->descriptorInfo(),
                                              bloomTarget->descriptorInfo()};
    VkWriteDescriptorSet glowWrites[2]{};
    for (uint32_t i = 0; i < 2; ++i) {
        glowWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        glowWrites[i].dstSet = glowInputSet;
        glowWrites[i].dstBinding = i;
        glowWrites[i].descriptorCount = 1;
        glowWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        glowWrites[i].pImageInfo = &glowInputs[i];
    }
    vkUpdateDescriptorSets(device, 2, glowWrites, 0, nullptr);

    glowTextureId = ImGui_ImplVulkan_AddTexture(glowTarget->getSampler(),
                                                glowTarget->getColorImageView(),
                                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void GlueScene::View::destroy() {
    // Before anything is freed. Every image below is sampled by whatever
    // frames are still in flight - the glow target most of all, since it is
    // the one the interface draws.
    if (ctx) vkDeviceWaitIdle(ctx->getDevice());

    // The glow passes, before the pool their descriptors came from.
    if (ctx) {
        VkDevice device = ctx->getDevice();
        if (bloomPipeline) { vkDestroyPipeline(device, bloomPipeline, nullptr); bloomPipeline = VK_NULL_HANDLE; }
        if (bloomPipelineLayout) { vkDestroyPipelineLayout(device, bloomPipelineLayout, nullptr); bloomPipelineLayout = VK_NULL_HANDLE; }
        if (bloomSetLayout) { vkDestroyDescriptorSetLayout(device, bloomSetLayout, nullptr); bloomSetLayout = VK_NULL_HANDLE; }
        if (glowPipeline) { vkDestroyPipeline(device, glowPipeline, nullptr); glowPipeline = VK_NULL_HANDLE; }
        if (glowPipelineLayout) { vkDestroyPipelineLayout(device, glowPipelineLayout, nullptr); glowPipelineLayout = VK_NULL_HANDLE; }
        if (glowSetLayout) { vkDestroyDescriptorSetLayout(device, glowSetLayout, nullptr); glowSetLayout = VK_NULL_HANDLE; }
    }
    if (glowTextureId != VK_NULL_HANDLE) {
        removeImGuiTexture(glowTextureId);
        glowTextureId = VK_NULL_HANDLE;
    }
    if (glowTarget) {
        if (ctx) glowTarget->destroy(ctx->getDevice(), ctx->getAllocator());
        glowTarget.reset();
    }
    if (bloomTarget) {
        if (ctx) bloomTarget->destroy(ctx->getDevice(), ctx->getAllocator());
        bloomTarget.reset();
    }
    bloomSceneSet = VK_NULL_HANDLE;
    glowInputSet = VK_NULL_HANDLE;
    glowApplied = false;
    glow = 0.0f;
    if (!ctx) return;
    VkDevice device = ctx->getDevice();
    VmaAllocator allocator = ctx->getAllocator();

    if (imguiTexture != VK_NULL_HANDLE) {
        removeImGuiTexture(imguiTexture);
        imguiTexture = VK_NULL_HANDLE;
    }
    if (models) { models->shutdown(); models.reset(); }
    for (uint32_t slot = 0; slot < kSlots; ++slot) {
        if (ubo[slot] != VK_NULL_HANDLE) rendering::destroy(allocator, ubo[slot], uboAlloc[slot]);
        ubo[slot] = VK_NULL_HANDLE;
        uboAlloc[slot] = VK_NULL_HANDLE;
        uboMapped[slot] = nullptr;
        perFrameSet[slot] = VK_NULL_HANDLE;
    }
    if (descPool != VK_NULL_HANDLE) { rendering::destroy(device, descPool); descPool = VK_NULL_HANDLE; }
    if (shadowView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowView, nullptr);
        shadowView = VK_NULL_HANDLE;
    }
    if (shadowImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, shadowImage, shadowAlloc);
        shadowImage = VK_NULL_HANDLE;
        shadowAlloc = VK_NULL_HANDLE;
    }
    if (target) { target->destroy(device, allocator); target.reset(); }

    loadedPath.clear();
    instanceId = 0;
    placed = false;
    everRecorded = false;
    appliedSequence = -1;
    appliedScale = 1.0f;
    width = 0;
    height = 0;
    ctx = nullptr;
}

bool GlueScene::View::loadScene(const std::string& rawPath, const GlueSceneState& scene) {
    if (!models || !assets) return false;
    const std::string m2Path = sceneModelPath(rawPath);

    clearScene();

    modelData = pipeline::M2Model{};
    if (!pipeline::loadM2WithSkin(*assets, m2Path, modelData)) {
        LOG_WARNING("GlueScene: no model at ", m2Path);
        return false;
    }

    // The camera is what decides whether this is drawn at all: a scene with
    // none cannot be placed, and is left out rather than framed by a guess.
    // Asked before anything is built from the model, so that a refusal
    // leaves nothing uploaded and no instance behind.
    if (glueCameraIndex(scene.cameraIndex, static_cast<int>(modelData.cameras.size())) < 0) {
        LOG_WARNING("GlueScene: ", m2Path, " carries no camera; not drawn");
        return false;
    }

    if (!models->loadModel(modelData, kSceneModelId)) {
        LOG_WARNING("GlueScene: could not upload ", m2Path);
        return false;
    }
    // No facing and no position: the camera is the placement. These scenes
    // are authored where the artist put them - the Northrend login dome sits
    // a couple of hundred units from its own origin - and nothing in the
    // interface says where to stand. The scale is the one thing the interface
    // does say, and every glue screen leaves it at one.
    appliedScale = scene.modelScale > 0.0f ? scene.modelScale : 1.0f;
    instanceId = models->createInstance(kSceneModelId, glm::vec3(0.0f),
                                        glm::vec3(0.0f), appliedScale);
    if (instanceId == 0) {
        LOG_WARNING("GlueScene: could not place ", m2Path);
        models->clear();
        return false;
    }
    placed = true;
    LOG_INFO("GlueScene: ", m2Path, " on show, ", modelData.cameras.size(), " camera(s), ",
             modelData.lights.size(), " light(s), ", modelData.particleEmitters.size(),
             " emitter(s)");
    return true;
}

void GlueScene::View::clearScene() {
    if (models && instanceId != 0) {
        models->removeInstance(instanceId);
        instanceId = 0;
    }
    if (models) models->clear();
    placed = false;
    appliedSequence = -1;
}

void GlueScene::View::applyScene(const GlueSceneState& scene) {
    // The fog and the lights, every frame: they are two vectors in the
    // per-frame block and comparing them would cost more than writing them.
    fog = glueSceneFogRange(scene.fog, scene.fogStart, scene.fogEnd);
    fogColor = glm::vec3(scene.fogColor[0], scene.fogColor[1], scene.fogColor[2]);
    lighting = glueSceneLighting(scene.lights.data(), scene.lights.size());
    // What the screen asked its scene to glow by. Zero is the ordinary case
    // and skips the pass entirely.
    glow = scene.glow;
    cameraIndex = scene.cameraIndex;

    if (!placed || instanceId == 0) return;

    // A scale change has to rebuild the instance: the renderer takes one when
    // an instance is made and has no way to change it afterwards. The model
    // itself stays uploaded, so this is cheap - and nothing in GlueXML scales
    // a backdrop, so in practice it never runs.
    const float wantScale = scene.modelScale > 0.0f ? scene.modelScale : 1.0f;
    if (std::abs(wantScale - appliedScale) > 1e-4f) {
        models->removeInstance(instanceId);
        appliedScale = wantScale;
        instanceId = models->createInstance(kSceneModelId, glm::vec3(0.0f),
                                            glm::vec3(0.0f), appliedScale);
        if (instanceId == 0) {
            placed = false;
            return;
        }
        appliedSequence = -1;
    }

    if (scene.sequence != appliedSequence) {
        // Sequence 0 is what every glue screen opens on.
        //
        // Passed through as an animation id, which is not quite what
        // SetSequence names - that is a position in the model's sequence list,
        // and this renderer looks the number up as M2Sequence::id. The two
        // agree at zero, and zero is the only value any glue screen asks for.
        //
        // Handed over whatever the model carries rather than checked first:
        // playAnimation falls back to the model's first sequence for a number
        // it cannot find, and says so once. Refusing to call it for a model
        // that numbers its one sequence something other than zero would leave
        // an authored scene standing perfectly still - and a scene's animation
        // is its snow and its light shafts, and nothing announces their
        // absence.
        const uint32_t want = scene.sequence < 0 ? 0u : static_cast<uint32_t>(scene.sequence);
        models->setInstanceAnimation(instanceId, want, true);
        appliedSequence = scene.sequence;
    }
}

void GlueScene::View::writePerFrame(uint32_t slot, const Camera& camera) {
    GPUPerFrameData block{};
    block.view = camera.getViewMatrix();
    block.projection = camera.getProjectionMatrix();
    block.lightSpaceMatrix = glm::mat4(1.0f);
    // The screen's own lighting for the scene, when it said any: up to four
    // directional lights merged into the one directional light and one
    // ambient colour this per-frame block has room for.
    //
    // When it said none, the model's own directional light, which is what
    // "ResetLights" means and what a screen that never adds lights is asking
    // for. An M2 light of type 0 is a directional light on the scene - the
    // Northrend login model carries exactly one, ambient (0.718, 0.831, 0.929)
    // at 1.3 with the diffuse term authored zero for the whole sequence - and
    // under it the scene's mountains and ground measure within a fifth of
    // the original client's. The interface's background default (see
    // glueSceneDefaultLights) was tried in its place: a 0.15 ambient over a
    // key light darkened the whole backdrop to a third of the original's,
    // so it is only the fallback for a model that carries no such light. A
    // type 1 light is a point light and is not scene lighting; it is left
    // out here rather than installed as a global.
    //
    // Two things the earlier version of this got wrong are fixed: the
    // direction is the light's own rather than a studio constant, and the
    // ambient is saturated at 1.0 the way the client saturates its lit
    // vertex colour, so 1.3 x (0.718, 0.831, 0.929) cannot multiply a texel
    // past itself. The bone the light is attached to is not applied - the
    // renderer keeps no bone matrices a caller can read - so the direction is
    // the rest one, which for a light with no diffuse term changes nothing.
    static const GlueSceneLighting kDefault = [] {
        const std::vector<GlueSceneLight> rig = glueSceneDefaultLights();
        return glueSceneLighting(rig.data(), rig.size());
    }();
    GlueSceneLighting fromModel;
    if (!lighting.authored && placed) {
        for (const pipeline::M2Light& light : modelData.lights) {
            if (light.type != 0 || !light.visible) continue;
            fromModel.authored = true;
            for (int c = 0; c < 3; ++c) {
                fromModel.ambientColor[c] = light.ambientColor[c] * light.ambientIntensity;
                fromModel.lightColor[c] = light.diffuseColor[c] * light.diffuseIntensity;
            }
            // The position of a directional light is where it shines from;
            // the shader takes the way the light travels.
            const float len = glm::length(light.position);
            if (std::isfinite(len) && len > 1e-6f) {
                const glm::vec3 travel = -light.position / len;
                fromModel.direction[0] = travel.x;
                fromModel.direction[1] = travel.y;
                fromModel.direction[2] = travel.z;
            } else {
                for (int c = 0; c < 3; ++c) fromModel.direction[c] = kDefault.direction[c];
            }
            break;
        }
    }
    const GlueSceneLighting& lit =
        lighting.authored ? lighting : (fromModel.authored ? fromModel : kDefault);
    {
        static bool said = false;
        if (!said) {
            said = true;
            LOG_WARNING("GLUE RIG src=",
                        lighting.authored ? "interface" : (fromModel.authored ? "model" : "default"),
                        " placed=", placed ? 1 : 0,
                        " lights=", modelData.lights.size(),
                        " amb=(", lit.ambientColor[0], ",", lit.ambientColor[1], ",",
                        lit.ambientColor[2], ") key=(", lit.lightColor[0], ",",
                        lit.lightColor[1], ",", lit.lightColor[2], ")");
        }
    }
    block.lightDir = glm::vec4(lit.direction[0], lit.direction[1], lit.direction[2], 0.0f);
    block.lightColor = glm::vec4(lit.lightColor[0], lit.lightColor[1], lit.lightColor[2],
                                 0.0f);
    // The ambient is a multiplier on every texel, and the client saturates its
    // lit colour at 1.0 before the texture. Four rows' ambient can sum past
    // it, and the login model's 1.3 does; past it a surface is brighter than
    // its own texture.
    block.ambientColor = glm::vec4(glm::min(glm::vec3(lit.ambientColor[0], lit.ambientColor[1],
                                                      lit.ambientColor[2]),
                                            glm::vec3(1.0f)),
                                   0.0f);
    block.viewPos = glm::vec4(camera.getPosition(), 0.0f);
    block.fogColor = glm::vec4(fogColor, 0.0f);
    // The scene's clock in the third slot: the model vertex shader reads its
    // wind and rustle time from there, and a zero froze every time-driven
    // vertex effect a glue scene has.
    block.fogParams = glm::vec4(fog.start, fog.end, sceneTime, 0.0f);
    // No shadow pass here, and sampling the stand-in through the shadow
    // path is not free of surprises on every driver.
    block.shadowParams = glm::vec4(0.0f);
    std::memcpy(uboMapped[slot], &block, sizeof(GPUPerFrameData));
}

void GlueScene::View::record(VkCommandBuffer cmd, uint32_t slot, const Camera& camera,
                             const FigureDraw& figures) {
    if (!ctx || !models || !target || !target->isValid()) return;
    slot = slot % kSlots;
    if (!uboMapped[slot]) return;

    // Bone buffers and descriptors, allocated before anything is recorded.
    //
    // For the context's own frame slot, not the one the block is written to:
    // the draw loop reads the slot back out of the context rather than taking
    // the one it was prepared for, so preparing another leaves every frame
    // that lands on the context's with no bone descriptor and nothing drawn.
    // The height a point sprite's pixel size is measured against is the
    // height that is drawn, not the allocation's: the two differ by up to
    // thirty-one rows, and setDrawSize can change the first at any time.
    models->setViewportHeight(static_cast<float>(usedHeight()));
    if (placed && instanceId != 0) models->prepareRender(ctx->getCurrentFrame(), camera);
    writePerFrame(slot, camera);

    target->beginPass(cmd, VkClearColorValue{{clearColor.r, clearColor.g, clearColor.b,
                                              clearColor.a}});
    // beginPass sized the viewport to the whole allocation; the scene belongs
    // in the part of it the owner will draw, so that the two are the same
    // size and nothing is resampled on the way to the screen.
    setUsedViewport(cmd, usedWidth(), usedHeight());
    // Figures first. They are opaque and write depth, so the scene's own
    // geometry is tested against them and its blended sheets and particles
    // fall in front of or behind them as their depth says - snow in front of
    // a character stays in front of the character.
    if (figures) figures(cmd, perFrameSet[slot], camera);
    if (placed && instanceId != 0) {
        models->render(cmd, perFrameSet[slot], camera);
        // And what the model emits. These two are not part of render(): the
        // world calls them itself, from the frame that owns the pass. A scene
        // owns its own pass, so it calls them itself as well - and without
        // them the Northrend login scene draws none of its forty emitters,
        // which is the snow, the frost on the wyrm and the light over the
        // citadel's spire.
        models->renderM2Particles(cmd, perFrameSet[slot]);
        models->renderM2Ribbons(cmd, perFrameSet[slot]);
    }
    target->endPass(cmd);
    // The glow, from the scene that was just drawn. Same command buffer, so it
    // reads the target in the layout endPass left it in. Only when something
    // was drawn to glow: a scene whose model never loaded is a cleared
    // target, and blurring that costs two passes for nothing.
    if ((placed && instanceId != 0) || figures) renderGlow(cmd);
    else glowApplied = false;
    everRecorded = true;
}

/// The scene's bright parts, blurred, and then the scene with them added.
///
/// Two passes of their own rather than a second pass over the scene: the
/// offscreen pass clears on begin, so going over the scene again would throw
/// it away, and the scene target is multisampled besides. The second pass
/// therefore writes a full-size image of its own, and that - not the scene
/// target - is what the owner draws.
void GlueScene::View::renderGlow(VkCommandBuffer cmd) {
    glowApplied = false;
    // WOWEE_NO_GLUE_GLOW turns it off, the way WOWEE_NO_GLUE_BACKDROP turns
    // off the scene: a glow of 0.08 is subtle enough that the only way to know
    // it is doing anything is to take the same frame without it.
    if (glow <= 0.0f || std::getenv("WOWEE_NO_GLUE_GLOW") ||
        !bloomPipeline || !bloomTarget || !bloomTarget->isValid() ||
        !glowPipeline || !glowTarget || !glowTarget->isValid()) {
        return;
    }

    // Once, with the figure the screen asked for.
    static bool saidGlow = false;
    if (!saidGlow) {
        saidGlow = true;
        const VkExtent2D e = bloomTarget->getExtent();
        LOG_INFO("GlueScene: glow ", glow, " through a ", e.width, "x", e.height, " pass");
    }

    bloomTarget->beginPass(cmd, VkClearColorValue{{0.0f, 0.0f, 0.0f, 0.0f}});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, bloomPipelineLayout,
                            0, 1, &bloomSceneSet, 0, nullptr);

    const VkExtent2D srcExtent = target->getExtent();
    // The scene only wrote the used sub-rect, so every tap is taken inside it.
    struct { float texelX, texelY, uvU, uvV; } push{
        srcExtent.width  > 0 ? 1.0f / static_cast<float>(srcExtent.width)  : 0.0f,
        srcExtent.height > 0 ? 1.0f / static_cast<float>(srcExtent.height) : 0.0f,
        usedU(), usedV(),
    };
    vkCmdPushConstants(cmd, bloomPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);

    setUsedViewport(cmd, std::max(1, usedWidth() / 2), std::max(1, usedHeight() / 2));

    // Three vertices and no vertex buffer - the fullscreen triangle the
    // post-process vertex shader builds from gl_VertexIndex.
    vkCmdDraw(cmd, 3, 1, 0, 0);
    bloomTarget->endPass(cmd);

    // And the scene with it added. Same command buffer again, so this reads
    // both images in the layout their own endPass left them in.
    glowTarget->beginPass(cmd, VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glowPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, glowPipelineLayout,
                            0, 1, &glowInputSet, 0, nullptr);
    // The bloom pass wrote the same fraction of its own half-size target as
    // the scene did of the full-size one, so one pair of scales serves both.
    const struct { float glow, uvU, uvV; } gpush{glow, usedU(), usedV()};
    vkCmdPushConstants(cmd, glowPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(gpush), &gpush);
    setUsedViewport(cmd, usedWidth(), usedHeight());
    vkCmdDraw(cmd, 3, 1, 0, 0);
    glowTarget->endPass(cmd);

    glowApplied = true;
}

// ---------------------------------------------------------------------------

GlueScene::GlueScene() = default;

GlueScene::~GlueScene() {
    // Only if the owner never said so. The device has to still be alive,
    // which is why shutdown() exists at all; reaching here with a live view
    // means a shutdown was missed, and freeing late is better than leaking.
    shutdown();
}

bool GlueScene::build(int width, int height, Renderer* renderer,
                      pipeline::AssetManager* assets) {
    if (width <= 0 || height <= 0 || !renderer || !assets) return false;
    if (view_ && view_->width == width && view_->height == height && view_->ctx) return true;
    shutdown();
    view_ = std::make_unique<View>();
    view_->assets = assets;
    if (!view_->build(width, height, renderer)) {
        view_->destroy();
        view_.reset();
        return false;
    }
    return true;
}

void GlueScene::shutdown() {
    if (view_) view_->destroy();
    view_.reset();
}

bool GlueScene::isBuilt() const { return view_ && view_->ctx != nullptr && view_->target; }
int GlueScene::width() const { return view_ ? view_->width : 0; }
int GlueScene::height() const { return view_ ? view_->height : 0; }

VkRenderPass GlueScene::renderPass() const {
    return isBuilt() ? view_->target->getRenderPass() : VK_NULL_HANDLE;
}

VkSampleCountFlagBits GlueScene::sampleCount() const {
    return isBuilt() ? view_->target->getSampleCount() : VK_SAMPLE_COUNT_1_BIT;
}

void GlueScene::setDrawSize(int width, int height) {
    if (!view_) return;
    view_->drawWidth = std::clamp(width, 0, view_->width);
    view_->drawHeight = std::clamp(height, 0, view_->height);
}

float GlueScene::drawAspect() const { return view_ ? view_->drawAspect() : 1.0f; }

void GlueScene::setClearColor(const glm::vec4& color) {
    if (view_) view_->clearColor = color;
}

bool GlueScene::show(const GlueSceneState& scene) {
    if (!view_ || !view_->models) return false;
    if (scene.model.empty()) {
        clear();
        return false;
    }
    // Tried once per path, whether or not it loaded: an install does not
    // grow a model between frames, and trying again every frame would be a
    // file search and a warning sixty times a second. `placed` says whether
    // the try succeeded; nothing else here treats the path as a success.
    if (view_->loadedPath != scene.model) {
        view_->loadedPath = scene.model;
        view_->loadScene(scene.model, scene);
    }
    view_->applyScene(scene);
    return view_->placed;
}

void GlueScene::clear() {
    if (!view_) return;
    view_->clearScene();
    view_->loadedPath.clear();
    view_->modelData = pipeline::M2Model{};
    // What the texture currently shows is gone with the scene. Left set,
    // textureId() would go on handing out the previous scene's picture until
    // the next pass happened to run.
    view_->everRecorded = false;
    view_->glowApplied = false;
    view_->sceneTime = 0.0f;
    view_->glow = 0.0f;
    view_->lighting = GlueSceneLighting{};
    view_->fog = glueSceneFogRange(false, 0.0f, 0.0f);
}

bool GlueScene::placed() const { return view_ && view_->placed; }

GlueSceneStage GlueScene::stage() const {
    GlueSceneStage out;
    if (!placed() || view_->modelData.cameras.empty()) return out;
    const int pick = glueCameraIndex(view_->cameraIndex,
                                     static_cast<int>(view_->modelData.cameras.size()));
    if (pick < 0) return out;
    const pipeline::M2Camera& cam = view_->modelData.cameras[static_cast<size_t>(pick)];
    out.cameraEye = cam.positionBase;
    out.cameraTarget = cam.targetBase;
    // Attachment 0 is the character's mark on the scene's ground.
    out.standPosition = cam.targetBase;
    for (const auto& att : view_->modelData.attachments) {
        if (att.id == 0) { out.standPosition = att.position; break; }
    }
    out.valid = true;
    return out;
}

bool GlueScene::frame(Camera& camera) const {
    if (!placed()) return false;
    const pipeline::M2Model& model = view_->modelData;
    const int pick = glueCameraIndex(view_->cameraIndex, static_cast<int>(model.cameras.size()));
    if (pick < 0) return false;
    if (pick != view_->cameraIndex) {
        LOG_WARNING("GlueScene: ", view_->loadedPath, " has ", model.cameras.size(),
                    " camera(s), so SetCamera(", view_->cameraIndex, ") falls back to the first");
    }
    const pipeline::M2Camera& cam = model.cameras[static_cast<size_t>(pick)];
    const float aspect = view_->drawAspect();
    const float eye[3] = {cam.positionBase.x, cam.positionBase.y, cam.positionBase.z};
    const float at[3] = {cam.targetBase.x, cam.targetBase.y, cam.targetBase.z};
    const GlueSceneFraming framing = glueSceneFraming(eye, at, cam.fov, aspect);
    if (!framing.usable) {
        LOG_WARNING("GlueScene: ", view_->loadedPath, " camera ", pick,
                    " cannot frame anything (fov ", cam.fov, " rad); not drawn");
        return false;
    }
    camera.setPosition(cam.positionBase);
    camera.setRotation(framing.yawDegrees, framing.pitchDegrees);
    camera.setFov(framing.fovYDegrees);
    camera.setAspectRatio(aspect);
    return true;
}

void GlueScene::update(float deltaTime, const Camera& camera) {
    if (!placed() || !view_->models) return;
    if (std::isfinite(deltaTime) && deltaTime > 0.0f) view_->sceneTime += deltaTime;
    // The view-projection is what this renderer culls against. A glue scene is
    // one instance and the camera stands inside it, so this is the camera's
    // own.
    view_->models->update(deltaTime, camera.getPosition(),
                          camera.getProjectionMatrix() * camera.getViewMatrix());
}

void GlueScene::record(VkCommandBuffer cmd, uint32_t slot, const Camera& camera,
                       const FigureDraw& figures) {
    if (!isBuilt()) return;
    view_->record(cmd, slot, camera, figures);
}

void GlueScene::composite(const Camera& camera) {
    if (!isBuilt()) return;
    // Its own submit rather than a pass inside the frame: the owner has no
    // frame to record into, and the alternative - a second command buffer
    // executed inside the open scene pass - depends on whether that pass was
    // begun for secondaries, which nothing here can ask. A glue screen draws
    // nothing else in three dimensions, so the wait costs a screen that has
    // frames to spare.
    view_->ctx->immediateSubmit([&](VkCommandBuffer cmd) { view_->record(cmd, 0, camera, {}); });
}

uint64_t GlueScene::textureId() const {
    if (!view_ || !view_->everRecorded) return 0;
    // The glow pass writes the scene with its glow already in it, so that is
    // the picture when it ran. When it did not - no glow on this screen, or
    // WOWEE_NO_GLUE_GLOW - the scene target is the picture and nothing else
    // was drawn or paid for.
    if (view_->glowApplied) return reinterpret_cast<uint64_t>(view_->glowTextureId);
    return reinterpret_cast<uint64_t>(view_->imguiTexture);
}

float GlueScene::textureU1() const { return view_ ? view_->usedU() : 1.0f; }
float GlueScene::textureV1() const { return view_ ? view_->usedV() : 1.0f; }

} // namespace wowee::rendering
