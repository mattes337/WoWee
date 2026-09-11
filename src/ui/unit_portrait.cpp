#include "ui/unit_portrait.hpp"
#include "game/equipment_hash.hpp"

#include "core/logger.hpp"
#include "game/game_handler.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_asset_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "rendering/camera.hpp"
#include "rendering/character_preview.hpp"
#include "rendering/m2_renderer.hpp"
#include "rendering/vk_shader.hpp"
#include "rendering/imgui_texture.hpp"
#include "rendering/renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_pipeline.hpp"
#include "rendering/vk_render_target.hpp"
#include "rendering/vk_utils.hpp"

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace wowee::ui {

namespace {

/// FNV-1a over what actually changes a character's look, so a reload happens
/// when the gear changes and not when a stat does.

} // namespace

UnitPortrait::UnitPortrait() = default;

UnitPortrait::~UnitPortrait() = default;

void UnitPortrait::update(game::GameHandler& gameHandler,
                          pipeline::AssetManager* assets,
                          rendering::Renderer* renderer, float deltaTime) {
    if (!assets || !renderer) return;

    // The character list is where a player's appearance is described; the unit
    // in the world carries a display id, not the pieces it was built from.
    const game::Character* self = nullptr;
    for (const auto& c : gameHandler.getCharacters()) {
        if (c.guid == gameHandler.getPlayerGuid()) { self = &c; break; }
    }
    if (!self) return;

    if (!preview_) {
        preview_ = std::make_unique<rendering::CharacterPreview>();
        // Small: this is drawn into a circle a few dozen pixels across, and the
        // cost of the pass scales with the target.
        initialized_ = preview_->initialize(assets, targetWidth_, targetHeight_);
        if (!initialized_) {
            LOG_WARNING("UnitPortrait: could not build the offscreen view");
            preview_.reset();
            return;
        }
        renderer->registerPreview(preview_.get());
        registered_ = true;
    }

    // What they are wearing now, not what they wore when they logged in.
    //
    // The character list carries the equipment SMSG_CHAR_ENUM described and
    // nothing updates it afterwards, so this portrait kept the gear the
    // character was wearing at the select screen for the whole session. The
    // inventory is the live answer, and it is the same one the target frame
    // gets when this character is the target - the two portraits are of the
    // same person and must not disagree. The login list stands in until the
    // item queries have come back, which is a moment at most.
    std::vector<game::EquipmentItem> worn;
    {
        std::array<uint32_t, 19> displayIds{};
        std::array<uint8_t, 19> invTypes{};
        if (gameHandler.getOtherPlayerEquipment(self->guid, displayIds, invTypes)) {
            for (size_t slot = 0; slot < displayIds.size(); ++slot) {
                if (displayIds[slot] == 0) continue;
                worn.push_back({.displayModel = displayIds[slot],
                                .inventoryType = invTypes[slot],
                                .enchantment = 0u});
            }
        }
        if (worn.empty()) worn = self->equipment;
    }

    const uint64_t equipHash = game::hashEquipmentAppearance(worn);
    const bool changed = (loadedGuid_ != self->guid) ||
                         (loadedAppearance_ != self->appearanceBytes) ||
                         (loadedFacialFeatures_ != self->facialFeatures) ||
                         (loadedEquipHash_ != equipHash);
    if (changed) {
        const uint8_t skin      =  self->appearanceBytes        & 0xFF;
        const uint8_t face      = (self->appearanceBytes >> 8)  & 0xFF;
        const uint8_t hairStyle = (self->appearanceBytes >> 16) & 0xFF;
        const uint8_t hairColor = (self->appearanceBytes >> 24) & 0xFF;

        // Declared before the model loads, so the racial backdrop is never
        // built in the first place.
        preview_->setTransparentBackground(true);
        if (preview_->loadCharacter(self->race, self->gender, skin, face,
                                    hairStyle, hairColor, self->facialFeatures,
                                    self->useFemaleModel)) {
            preview_->applyEquipment(worn);
            // After the model, because its bounds are what the framing is
            // measured against.
            if (framing_ == Framing::Face) {
                preview_->setPortraitFraming();
            } else {
                // The whole figure, facing the viewer. resetView is the
                // character-select framing, which is what a paperdoll wants.
                preview_->resetView();
            }
        }
        // Logged because a portrait that rebuilds every frame looks like one
        // that flickers, and the two are indistinguishable from outside.
        LOG_INFO("UnitPortrait: rebuilt for guid ", self->guid,
                 " appearance ", self->appearanceBytes);
        loadedCreaturePath_.clear();
        loadedGuid_ = self->guid;
        loadedAppearance_ = self->appearanceBytes;
        loadedFacialFeatures_ = self->facialFeatures;
        loadedEquipHash_ = equipHash;
    }

    preview_->update(deltaTime);
    preview_->render();
    preview_->requestComposite();
}

bool UnitPortrait::updatePlayer(uint8_t race, uint8_t gender,
                                uint32_t appearanceBytes, uint8_t facialFeatures,
                                const std::vector<game::EquipmentItem>& equipment,
                                pipeline::AssetManager* assets,
                                rendering::Renderer* renderer, float deltaTime) {
    if (!assets || !renderer) return false;

    if (!preview_) {
        preview_ = std::make_unique<rendering::CharacterPreview>();
        initialized_ = preview_->initialize(assets, targetWidth_, targetHeight_);
        if (!initialized_) {
            LOG_WARNING("UnitPortrait: could not build the offscreen view");
            preview_.reset();
            return false;
        }
        renderer->registerPreview(preview_.get());
        registered_ = true;
    }

    // The same three keys the player's own portrait compares, minus the guid -
    // this is asked per unit and the caller has already decided which.
    const uint64_t equipHash = game::hashEquipmentAppearance(equipment);
    const bool changed = (loadedAppearance_ != appearanceBytes) ||
                         (loadedFacialFeatures_ != facialFeatures) ||
                         (loadedRace_ != race) || (loadedGender_ != gender) ||
                         (loadedEquipHash_ != equipHash) ||
                         (loadedBake_ != pendingBake_) ||
                         !loadedCreaturePath_.empty();
    if (changed) {
        const uint8_t skin      =  appearanceBytes        & 0xFF;
        const uint8_t face      = (appearanceBytes >> 8)  & 0xFF;
        const uint8_t hairStyle = (appearanceBytes >> 16) & 0xFF;
        const uint8_t hairColor = (appearanceBytes >> 24) & 0xFF;

        preview_->setTransparentBackground(true);
        if (preview_->loadCharacter(static_cast<game::Race>(race),
                                    static_cast<game::Gender>(gender),
                                    skin, face, hairStyle, hairColor,
                                    facialFeatures, gender == 1)) {
            // After the model, because applyEquipment reads its geosets, and
            // only where there is something to apply - an empty list is
            // "nothing known yet", and dressing a model in it strips it.
            if (!equipment.empty()) preview_->applyEquipment(equipment);
            // After the equipment, because both write the skin slot and the
            // bake is the more complete answer: it already has the armour on
            // it, which is the half applyEquipment cannot composite for an NPC.
            if (!pendingBake_.empty()) preview_->setBakedSkin(pendingBake_);
            if (framing_ == Framing::Face) preview_->setPortraitFraming();
            else                           preview_->resetView();
        }
        LOG_INFO("UnitPortrait: rebuilt for player race ", static_cast<int>(race),
                 " appearance ", appearanceBytes);
        loadedCreaturePath_.clear();
        loadedGuid_ = 0;
        loadedRace_ = race;
        loadedGender_ = gender;
        loadedAppearance_ = appearanceBytes;
        loadedFacialFeatures_ = facialFeatures;
        loadedEquipHash_ = equipHash;
        loadedBake_ = pendingBake_;
    }

    preview_->update(deltaTime);
    preview_->render();
    preview_->requestComposite();
    return preview_->isModelLoaded();
}

bool UnitPortrait::updateCreature(const std::string& m2Path,
                                  const std::vector<std::pair<uint32_t, std::string>>& skins,
                                  pipeline::AssetManager* assets,
                                  rendering::Renderer* renderer,
                                  float deltaTime) {
    if (!assets || !renderer || m2Path.empty()) return false;

    if (!preview_) {
        preview_ = std::make_unique<rendering::CharacterPreview>();
        initialized_ = preview_->initialize(assets, targetWidth_, targetHeight_);
        if (!initialized_) {
            LOG_WARNING("UnitPortrait: could not build the offscreen view");
            preview_.reset();
            return false;
        }
        renderer->registerPreview(preview_.get());
        registered_ = true;
    }

    if (loadedCreaturePath_ != m2Path) {
        // Declared before the model loads, so the racial backdrop is never
        // built in the first place - the same order loadCharacter needs.
        preview_->setTransparentBackground(true);
        if (preview_->loadCreature(m2Path, skins)) {
            if (framing_ == Framing::Face) {
                preview_->setPortraitFraming();
            } else {
                preview_->resetView();
            }
        }
        LOG_INFO("UnitPortrait: rebuilt for creature ", m2Path);
        loadedCreaturePath_ = m2Path;
        // A player and a creature share one preview, so loading either has to
        // forget what the other was, or switching back would find nothing
        // changed and keep drawing the wrong one.
        loadedGuid_ = 0;
        loadedAppearance_ = 0;
        loadedFacialFeatures_ = 0;
        loadedEquipHash_ = 0;
    }

    preview_->update(deltaTime);
    preview_->render();
    preview_->requestComposite();
    return preview_->isModelLoaded();
}

uint64_t UnitPortrait::textureId() const {
    if (!preview_) return 0;
    return reinterpret_cast<uint64_t>(preview_->getTextureId());
}

void UnitPortrait::rotate(float yawDelta) {
    if (preview_ && yawDelta != 0.0f) preview_->rotate(yawDelta);
}

void UnitPortrait::shutdown(rendering::Renderer* renderer) {
    if (preview_ && registered_ && renderer) renderer->unregisterPreview(preview_.get());
    registered_ = false;
    preview_.reset();
    initialized_ = false;
}

// ---------------------------------------------------------------------------
// GlueBackdrop
// ---------------------------------------------------------------------------

namespace {

/// One model at a time, so the id is a constant rather than a counter: a glue
/// screen shows one scene and swapping screens replaces it.
constexpr uint32_t kBackdropModelId = 9995;

} // namespace

/// Everything the backdrop needs a Vulkan device for, kept out of the header
/// for the reason UnitPortrait keeps its texture handle as an integer: the
/// widget tree includes this and must not include Vulkan.
struct GlueBackdrop::View {
    pipeline::AssetManager* assets = nullptr;
    rendering::VkContext* ctx = nullptr;

    /// The renderer for this scene.
    ///
    /// M2Renderer rather than CharacterRenderer: a glue backdrop is an M2
    /// scene, not a figure. The login screen's model carries forty particle
    /// emitters and that is what its glow is made of - the frost on the wyrm's
    /// body, the burst over the citadel's spire - and only this renderer
    /// simulates and draws them.
    std::unique_ptr<rendering::M2Renderer> models;
    /// The loaded model, kept here because M2Renderer hands back no view of
    /// what it uploaded and frameThrough needs the cameras.
    pipeline::M2Model modelData;
    std::unique_ptr<rendering::Camera> camera;
    std::unique_ptr<rendering::VkRenderTarget> target;

    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkBuffer ubo = VK_NULL_HANDLE;
    VmaAllocation uboAlloc = VK_NULL_HANDLE;
    void* uboMapped = nullptr;
    VkDescriptorSet perFrameSet = VK_NULL_HANDLE;

    // The renderer's per-frame set declares a shadow map at binding 1. There
    // is no shadow pass here, so it is a 1x1 depth image cleared to "nothing
    // in the way" - the same stand-in CharacterPreview uses.
    VkImage shadowImage = VK_NULL_HANDLE;
    VkImageView shadowView = VK_NULL_HANDLE;
    VmaAllocation shadowAlloc = VK_NULL_HANDLE;

    VkDescriptorSet imguiTexture = VK_NULL_HANDLE;

    // The screen's glow, as a second texture drawn over the scene.
    //
    // ModelFFX carries a glow figure on five of the glue screens and the model
    // shader has no glow term. It is done here, after the scene is drawn, the
    // way a bloom is normally done: take what is bright, blur it, and let the
    // interface lay it back over. Half resolution, because a blur is what this
    // is and the extra detail would only be thrown away.
    std::unique_ptr<rendering::VkRenderTarget> bloomTarget;
    VkDescriptorSetLayout bloomSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet bloomSceneSet = VK_NULL_HANDLE;   // samples the scene
    VkPipelineLayout bloomPipelineLayout = VK_NULL_HANDLE;
    VkPipeline bloomPipeline = VK_NULL_HANDLE;

    // And the pass that adds it back. Full size, because its result is the
    // picture: the interface draws this image rather than the scene whenever
    // the screen asked for a glow. It has to be a pass of its own - an ImGui
    // draw list has one blend state, and an alpha blend cannot add.
    std::unique_ptr<rendering::VkRenderTarget> glowTarget;
    VkDescriptorSetLayout glowSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet glowInputSet = VK_NULL_HANDLE;    // samples the scene and the bloom
    VkPipelineLayout glowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline glowPipeline = VK_NULL_HANDLE;
    VkDescriptorSet glowTextureId = VK_NULL_HANDLE;   // what the interface draws
    /// Set by the pass, cleared when it does not run. The interface reads the
    /// scene straight when this is false, so a screen with no glow - or a run
    /// with WOWEE_NO_GLUE_GLOW - costs nothing and shows nothing.
    bool glowApplied = false;
    float glow = 0.0f;

    bool buildBloom(int w, int h);
    void renderGlow(VkCommandBuffer cmd);

    int width = 0;
    int height = 0;
    /// What the interface actually draws the image into, in pixels.
    ///
    /// Not the target's size. The target is this rounded up to a multiple of
    /// 32 so a window dragged by a few pixels does not rebuild it, and at
    /// 1280x720 that makes a 1280x736 image drawn into a 1280x720 rect - a
    /// resample that drops one row in every forty-six. It is not subtle where
    /// the picture has edges: over the frost wyrm on the login screen it reads
    /// as pale bands across the wing, forty-five pixels apart, which is what
    /// "boxes of light" turned out to be.
    ///
    /// So the passes draw into the top-left drawWidth x drawHeight of their
    /// targets and the interface samples exactly that much, one texel to one
    /// pixel. The rest of the allocation is never written or read.
    int drawWidth = 0;
    int drawHeight = 0;
    [[nodiscard]] int usedWidth() const { return drawWidth > 0 ? drawWidth : width; }
    [[nodiscard]] int usedHeight() const { return drawHeight > 0 ? drawHeight : height; }
    [[nodiscard]] float drawAspect() const {
        const int h = usedHeight();
        return h > 0 ? static_cast<float>(usedWidth()) / static_cast<float>(h) : 1.0f;
    }
    /// The fraction of each target the passes above actually wrote, which is
    /// what the interface has to sample.
    [[nodiscard]] float usedU() const {
        return width > 0 ? static_cast<float>(usedWidth()) / static_cast<float>(width) : 1.0f;
    }
    [[nodiscard]] float usedV() const {
        return height > 0 ? static_cast<float>(usedHeight()) / static_cast<float>(height) : 1.0f;
    }
    std::string loadedPath;
    /// Set when the loaded model was placed by its own camera. A scene that
    /// carries none is left undrawn rather than framed by a guess.
    bool placed = false;
    bool everComposited = false;
    uint32_t instanceId = 0;

    /// What the model on screen was last built and lit from, so a screen that
    /// says the same thing every frame - and they all do, because the client
    /// reads it back rather than being told when it changes - reloads nothing.
    int appliedCamera = -1;
    int appliedSequence = -1;
    float appliedScale = 1.0f;
    /// The fog and lights, applied straight into the per-frame block on every
    /// composite. Cheap enough that there is nothing to compare against.
    GlueSceneFogRange fog{9999.0f, 10000.0f};
    glm::vec3 fogColor{0.0f, 0.0f, 0.0f};
    GlueSceneLighting lighting;

    bool build(int w, int h, rendering::Renderer* renderer);
    void destroy();
    bool loadScene(const std::string& rawPath, const GlueSceneState& scene);
    /// Point the camera through the model's `index`'th own camera. False when
    /// the model has none, or the one it has cannot frame anything.
    bool frameThrough(int index);
    void applyScene(const GlueSceneState& scene);
    void composite();
};

/// Point the viewport and scissor at the top-left w x h of whatever pass is
/// open, which is the part of the target that carries the picture.
static void setUsedViewport(VkCommandBuffer cmd, int w, int h) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(std::max(1, w)),
                  static_cast<float>(std::max(1, h)), 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {static_cast<uint32_t>(std::max(1, w)),
                         static_cast<uint32_t>(std::max(1, h))}};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

bool GlueBackdrop::View::build(int w, int h, rendering::Renderer* renderer) {
    ctx = renderer->getVkContext();
    const VkDescriptorSetLayout perFrameLayout = renderer->getPerFrameSetLayout();
    if (!ctx || perFrameLayout == VK_NULL_HANDLE) return false;

    width = w;
    height = h;

    VkDevice device = ctx->getDevice();
    VmaAllocator allocator = ctx->getAllocator();

    target = std::make_unique<rendering::VkRenderTarget>();
    if (!target->create(*ctx, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                        VK_FORMAT_R8G8B8A8_UNORM, true, VK_SAMPLE_COUNT_4_BIT)) {
        LOG_WARNING("GlueBackdrop: could not create the ", width, "x", height, " view");
        target.reset();
        return false;
    }

    // The widget is handed this image on the first frame the model loads, which
    // is the frame before the first pass has run. Put it in the layout ImGui
    // samples from now, so that frame reads a black image rather than one in
    // UNDEFINED layout.
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
        rendering::cmdPipelineBarrier2(cmd, dep);
    });

    models = std::make_unique<rendering::M2Renderer>();
    // Before initialize, as setSkyMode wants: this is one authored scene, not
    // a field of world doodads, so its particles are not damped.
    models->setSceneMode(true);
    if (!models->initialize(ctx, perFrameLayout, assets, target->getRenderPass(),
                            target->getSampleCount())) {
        LOG_WARNING("GlueBackdrop: could not build the model renderer");
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
            LOG_WARNING("GlueBackdrop: could not create the shadow stand-in");
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
            LOG_WARNING("GlueBackdrop: could not create the shadow stand-in view");
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
            rendering::cmdPipelineBarrier2(cmd, depA);

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
            rendering::cmdPipelineBarrier2(cmd, depB);
        });
    }

    // --- the per-frame set the model pipelines read their matrices from ---
    {
        // Three sets: the model pipelines' per-frame block, the bloom pass's
        // one scene sampler, and the glow pass's scene-and-bloom pair. The
        // pool said one set and two descriptors when the glow passes were
        // added, which is a driver-dependent failure - lavapipe handed out the
        // extra sets anyway and the glow worked, and a stricter driver would
        // have returned VK_ERROR_OUT_OF_POOL_MEMORY and quietly lost it.
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = 1;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sizes[1].descriptorCount = 4;
        VkDescriptorPoolCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.maxSets = 3;
        ci.poolSizeCount = 2;
        ci.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(device, &ci, nullptr, &descPool) != VK_SUCCESS) {
            LOG_WARNING("GlueBackdrop: could not create the descriptor pool");
            return false;
        }

        VkBufferCreateInfo bufInfo{.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufInfo.size = sizeof(rendering::GPUPerFrameData);
        bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo mapInfo{};
        if (vmaCreateBuffer(allocator, &bufInfo, &allocInfo, &ubo, &uboAlloc, &mapInfo)
                != VK_SUCCESS) {
            LOG_WARNING("GlueBackdrop: could not create the per-frame buffer");
            return false;
        }
        uboMapped = mapInfo.pMappedData;

        VkDescriptorSetAllocateInfo setAlloc{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAlloc.descriptorPool = descPool;
        setAlloc.descriptorSetCount = 1;
        setAlloc.pSetLayouts = &perFrameLayout;
        if (vkAllocateDescriptorSets(device, &setAlloc, &perFrameSet) != VK_SUCCESS) {
            LOG_WARNING("GlueBackdrop: could not allocate the per-frame set");
            return false;
        }

        VkDescriptorBufferInfo descBuf{};
        descBuf.buffer = ubo;
        descBuf.offset = 0;
        descBuf.range = sizeof(rendering::GPUPerFrameData);
        VkDescriptorImageInfo shadowImg{};
        // The sampler is ignored: binding 1 of the renderer's layout declares
        // an immutable comparison sampler of its own.
        shadowImg.imageView = shadowView;
        shadowImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[2]{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = perFrameSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].pBufferInfo = &descBuf;
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = perFrameSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &shadowImg;
        vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
    }

    camera = std::make_unique<rendering::Camera>();
    camera->setAspectRatio(drawAspect());

    imguiTexture = ImGui_ImplVulkan_AddTexture(target->getSampler(),
                                               target->getColorImageView(),
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!buildBloom(width, height)) {
        // The scene still draws; only its glow is missing. Said once, because
        // a glow of 0.08 going quietly absent is exactly the kind of thing
        // that gets rediscovered from a screenshot months later.
        LOG_WARNING("GlueBackdrop: no glow pass - the scene draws without it");
    }

    LOG_INFO("GlueBackdrop: view built (", width, "x", height, ")");
    return true;
}

/// The half-size target, pipeline and descriptor the glow pass needs.
///
/// Half size on purpose: the pass blurs, and detail thrown into a blur is
/// detail paid for and discarded. It also halves the taps' cost, which is the
/// whole of what this costs per frame.
bool GlueBackdrop::View::buildBloom(int w, int h) {
    VkDevice device = ctx->getDevice();

    bloomTarget = std::make_unique<rendering::VkRenderTarget>();
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

    rendering::VkShaderModule vert, frag;
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

    rendering::PipelineBuilder builder;
    bloomPipeline = builder
        .setShaders(vs, fs)
        .setVertexInput({}, {})          // the triangle comes from gl_VertexIndex
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest()
        .setColorBlendAttachment(rendering::PipelineBuilder::blendDisabled())
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
    glowTarget = std::make_unique<rendering::VkRenderTarget>();
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

    rendering::VkShaderModule glowFrag;
    if (!glowFrag.loadFromFile(device, "assets/shaders/glue_glow.frag.spv")) return false;
    VkPipelineShaderStageCreateInfo gfs{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    gfs.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    gfs.module = glowFrag.getModule();
    gfs.pName = "main";

    rendering::PipelineBuilder glowBuilder;
    glowPipeline = glowBuilder
        .setShaders(vs, gfs)             // the same fullscreen-triangle vertex stage
        .setVertexInput({}, {})
        .setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        .setRasterization(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE)
        .setNoDepthTest()
        .setColorBlendAttachment(rendering::PipelineBuilder::blendDisabled())
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

void GlueBackdrop::View::destroy() {

    // Before anything is freed. Every image below is sampled by whatever
    // frames are still in flight - the glow target most of all, since it is
    // the one the interface draws - and the wait used to sit halfway down,
    // after the glow pass had already been torn down.
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
        rendering::removeImGuiTexture(glowTextureId);
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

    if (imguiTexture != VK_NULL_HANDLE) rendering::removeImGuiTexture(imguiTexture);
    if (models) { models->shutdown(); models.reset(); }
    camera.reset();
    if (ubo != VK_NULL_HANDLE) rendering::destroy(allocator, ubo, uboAlloc);
    uboMapped = nullptr;
    perFrameSet = VK_NULL_HANDLE;
    if (descPool != VK_NULL_HANDLE) rendering::destroy(device, descPool);
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
    everComposited = false;
    appliedCamera = -1;
    appliedSequence = -1;
    appliedScale = 1.0f;
    width = 0;
    height = 0;
    ctx = nullptr;
}

bool GlueBackdrop::View::loadScene(const std::string& rawPath, const GlueSceneState& scene) {
    if (!models || !assets || !camera) return false;

    // .mdx means .m2, the same way .tga means .blp elsewhere in this
    // interface. Blizzard's markup still names models by the extension the
    // format carried before 3.x and no .mdx has shipped in the archives since;
    // the original client takes the name and loads the .m2 beside it.
    //
    // AccountLogin_OnLoad is the one that matters:
    // SetModel("...UI_MainMenu_Northrend.mdx") is the only statement of what
    // the login screen looks like, and taking that extension at its word left
    // the whole screen black with one line in the log to say why.
    std::string m2Path = rawPath;
    if (m2Path.size() > 4) {
        std::string tail = m2Path.substr(m2Path.size() - 4);
        for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail == ".mdx") m2Path.replace(m2Path.size() - 4, 4, ".m2");
    }

    if (instanceId != 0) {
        models->removeInstance(instanceId);
        instanceId = 0;
    }
    models->clear();
    placed = false;
    appliedCamera = -1;
    appliedSequence = -1;

    modelData = pipeline::M2Model{};
    if (!pipeline::loadM2WithSkin(*assets, m2Path, modelData)) {
        LOG_WARNING("GlueBackdrop: no model at ", m2Path);
        return false;
    }

    if (!models->loadModel(modelData, kBackdropModelId)) {
        LOG_WARNING("GlueBackdrop: could not upload ", m2Path);
        return false;
    }
    // No facing and no position: the camera is the placement. These scenes are
    // authored where the artist put them - the Northrend login dome sits a
    // couple of hundred units from its own origin - and nothing in the
    // interface says where to stand. The scale is the one thing the interface
    // does say, and every glue screen leaves it at one.
    appliedScale = scene.modelScale > 0.0f ? scene.modelScale : 1.0f;
    instanceId = models->createInstance(kBackdropModelId, glm::vec3(0.0f),
                                        glm::vec3(0.0f), appliedScale);
    if (instanceId == 0) {
        LOG_WARNING("GlueBackdrop: could not place ", m2Path);
        return false;
    }
    // The camera is what decides whether this is drawn at all, so it is asked
    // for after the model is up but before anything says the scene is placed.
    if (!frameThrough(scene.cameraIndex)) return false;
    placed = true;
    return true;
}

bool GlueBackdrop::View::frameThrough(int index) {
    const pipeline::M2Model* model = models ? &modelData : nullptr;
    if (model == nullptr || model->cameras.empty() || camera == nullptr) return false;

    const int pick = glueCameraIndex(index, static_cast<int>(model->cameras.size()));
    if (pick < 0) {
        // A scene with no camera cannot be placed at all, and is left out
        // rather than framed by a guess.
        LOG_WARNING("GlueBackdrop: ", loadedPath, " carries no camera; not drawn");
        return false;
    }
    if (pick != index) {
        LOG_WARNING("GlueBackdrop: ", loadedPath, " has ", model->cameras.size(),
                    " camera(s), so SetCamera(", index, ") falls back to the first");
    }

    const pipeline::M2Camera& cam = model->cameras[static_cast<size_t>(pick)];
    const float aspect = drawAspect();
    const float eye[3] = {cam.positionBase.x, cam.positionBase.y, cam.positionBase.z};
    const float at[3] = {cam.targetBase.x, cam.targetBase.y, cam.targetBase.z};
    const GlueBackdropFraming framing = glueBackdropFraming(eye, at, cam.fov, aspect);
    if (!framing.usable) {
        LOG_WARNING("GlueBackdrop: ", loadedPath, " camera ", pick,
                    " cannot frame anything (fov ", cam.fov, " rad); not drawn");
        return false;
    }

    camera->setPosition(cam.positionBase);
    camera->setRotation(framing.yawDegrees, framing.pitchDegrees);
    camera->setFov(framing.fovYDegrees);
    camera->setAspectRatio(aspect);
    appliedCamera = index;

    LOG_INFO("GlueBackdrop: ", loadedPath, " through camera ", pick, " at (",
             cam.positionBase.x, ",", cam.positionBase.y, ",", cam.positionBase.z,
             ") looking at (", cam.targetBase.x, ",", cam.targetBase.y, ",",
             cam.targetBase.z, "), ", camera->getFovDegrees(), " deg vertical");
    return true;
}

void GlueBackdrop::View::applyScene(const GlueSceneState& scene) {
    // The fog and the lights, every frame: they are two vectors in the
    // per-frame block and comparing them would cost more than writing them.
    fog = glueSceneFogRange(scene.fog, scene.fogStart, scene.fogEnd);
    fogColor = glm::vec3(scene.fogColor[0], scene.fogColor[1], scene.fogColor[2]);
    lighting = glueSceneLighting(scene.lights.data(), scene.lights.size());
    // What the screen asked its scene to glow by. Zero is the ordinary case
    // and skips the pass entirely.
    glow = scene.glow;

    if (!placed || instanceId == 0) return;

    // A scale change has to rebuild the instance: the renderer takes one when
    // an instance is made and has no way to change it afterwards. The model
    // itself stays uploaded, so this is cheap - and nothing in GlueXML scales
    // a backdrop, so in practice it never runs.
    const float wantScale = scene.modelScale > 0.0f ? scene.modelScale : 1.0f;
    if (std::abs(wantScale - appliedScale) > 1e-4f) {
        models->removeInstance(instanceId);
        appliedScale = wantScale;
        instanceId = models->createInstance(kBackdropModelId, glm::vec3(0.0f),
                                           glm::vec3(0.0f), appliedScale);
        if (instanceId == 0) {
            placed = false;
            return;
        }
        appliedSequence = -1;
    }

    if (scene.cameraIndex != appliedCamera) {
        // A camera that cannot frame anything leaves the previous one in
        // place; the scene was drawable a moment ago and half a swap is worse
        // than no swap.
        if (!frameThrough(scene.cameraIndex)) appliedCamera = scene.cameraIndex;
    }

    if (scene.sequence != appliedSequence) {
        // Sequence 0 is what every glue screen opens on; the backdrop used to
        // be given it unconditionally, which was right by accident.
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
        // an authored scene standing perfectly still - which is the thing this
        // has to be careful not to do, because a glue backdrop's animation is
        // its snow and its light shafts and nothing announces their absence.
        const uint32_t want = scene.sequence < 0 ? 0u : static_cast<uint32_t>(scene.sequence);
        models->setInstanceAnimation(instanceId, want, true);
        appliedSequence = scene.sequence;
    }
}

void GlueBackdrop::View::composite() {
    if (!ctx || !models || !camera || !target || !target->isValid() || !uboMapped) return;
    if (!placed || instanceId == 0) return;

    // Bone buffers and descriptors, allocated before anything is recorded.
    //
    // For the context's own frame slot, not a fixed one: the draw loop reads
    // the slot back out of the context rather than taking the one it was
    // prepared for, so preparing slot 0 every time leaves every frame that
    // lands on slot 1 with no bone descriptor and nothing drawn.
    // Half the frames rendered the scene and half rendered the clear colour,
    // which on a screenshot reads as "the backdrop does not work" about as
    // often as it reads as "it does".
    models->prepareRender(ctx->getCurrentFrame(), *camera);

    rendering::GPUPerFrameData ubo{};
    ubo.view = camera->getViewMatrix();
    ubo.projection = camera->getProjectionMatrix();
    ubo.lightSpaceMatrix = glm::mat4(1.0f);
    // The interface's own lighting for the scene, when the screen said any.
    //
    // GlueParent.lua's SetLighting is where that comes from: a fog colour and
    // range out of CharModelFogInfo and up to four directional lights out of
    // RaceLights, merged into the one directional light and one ambient colour
    // this per-frame block has room for.
    //
    // A screen that adds no lights keeps the studio rig below, which is what
    // the character preview lights its racial backdrops with. That is the
    // ordinary case rather than the exception: ResetLights means "use the
    // model's own lights", which this client does not read, and the stock
    // login screen never calls SetLighting at all.
    if (lighting.authored) {
        ubo.lightDir = glm::vec4(lighting.direction[0], lighting.direction[1],
                                 lighting.direction[2], 0.0f);
        ubo.lightColor = glm::vec4(lighting.lightColor[0], lighting.lightColor[1],
                                   lighting.lightColor[2], 0.0f);
        ubo.ambientColor = glm::vec4(lighting.ambientColor[0], lighting.ambientColor[1],
                                     lighting.ambientColor[2], 0.0f);
    } else {
        ubo.lightDir = glm::vec4(glm::normalize(glm::vec3(0.5f, -0.7f, 0.5f)), 0.0f);
        ubo.lightColor = glm::vec4(1.0f, 0.95f, 0.9f, 0.0f);
        ubo.ambientColor = glm::vec4(0.45f, 0.45f, 0.5f, 0.0f);
    }
    ubo.viewPos = glm::vec4(camera->getPosition(), 0.0f);
    ubo.fogColor = glm::vec4(fogColor, 0.0f);
    ubo.fogParams = glm::vec4(fog.start, fog.end, 0.0f, 0.0f);
    ubo.shadowParams = glm::vec4(0.0f);
    std::memcpy(uboMapped, &ubo, sizeof(rendering::GPUPerFrameData));

    // Its own submit rather than a pass inside the frame. The frame's
    // off-screen pre-passes are the renderer's list and it holds character
    // previews; this is not one, and the alternative - a second command buffer
    // executed inside the open scene pass - depends on whether that pass was
    // begun for secondaries, which nothing here can ask. A glue screen draws
    // nothing else in three dimensions, so the wait costs a screen that has
    // frames to spare.
    ctx->immediateSubmit([&](VkCommandBuffer cmd) {
        target->beginPass(cmd, VkClearColorValue{{0.0f, 0.0f, 0.0f, 1.0f}});
        // beginPass sized the viewport to the whole allocation; the scene
        // belongs in the part of it the interface will draw, so that the two
        // are the same size and nothing is resampled on the way to the screen.
        setUsedViewport(cmd, usedWidth(), usedHeight());
        models->render(cmd, perFrameSet, *camera);
        // And what the model emits. These two are not part of render(): the
        // world calls them itself, from the frame that owns the pass. A glue
        // backdrop owns its own pass, so it calls them itself as well - and
        // without them the Northrend login scene draws none of its forty
        // emitters, which is the snow, the frost on the wyrm and the light
        // over the citadel's spire.
        models->renderM2Particles(cmd, perFrameSet);
        models->renderM2Ribbons(cmd, perFrameSet);
        target->endPass(cmd);
        // The glow, from the scene that was just drawn. Inside the same submit
        // so it reads the target in the layout endPass left it in.
        renderGlow(cmd);
    });
    everComposited = true;
}

/// The scene's bright parts, blurred, and then the scene with them added.
///
/// Two passes of their own rather than a second pass over the scene: this
/// renderer's off-screen pass clears on begin, so going over the scene again
/// would throw it away, and the scene target is multisampled besides. The
/// second pass therefore writes a full-size image of its own, and that - not
/// the scene target - is what the interface draws.
///
/// The alternative, handing the interface a bloom texture to lay on top, is
/// what this replaced. An ImGui draw list has one blend state and it is an
/// alpha blend, which replaces what it covers; adding light through it is not
/// possible at any alpha.
void GlueBackdrop::View::renderGlow(VkCommandBuffer cmd) {
    glowApplied = false;
    // WOWEE_NO_GLUE_GLOW turns it off, the way WOWEE_NO_GLUE_BACKDROP turns
    // off the scene: a glow of 0.08 is subtle enough that the only way to know
    // it is doing anything is to take the same frame without it.
    if (glow <= 0.0f || std::getenv("WOWEE_NO_GLUE_GLOW") ||
        !bloomPipeline || !bloomTarget || !bloomTarget->isValid() ||
        !glowPipeline || !glowTarget || !glowTarget->isValid()) {
        return;
    }

    // Once, with the figure the screen asked for. A glow of 0.08 is meant to
    // be subtle, so "is it running at all" is not a question a screenshot
    // answers on its own.
    static bool saidGlow = false;
    if (!saidGlow) {
        saidGlow = true;
        const VkExtent2D e = bloomTarget->getExtent();
        LOG_INFO("GlueBackdrop: glow ", glow, " through a ", e.width, "x", e.height,
                 " pass");
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

    // And the scene with it added. Same submit again, so this reads both
    // images in the layout their own endPass left them in.
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

GlueBackdrop::GlueBackdrop() = default;

GlueBackdrop::~GlueBackdrop() {
    // Only if the owner never said so. The device has to still be alive, which
    // is why shutdown() exists at all; reaching here with a live view means a
    // shutdown was missed, and freeing late is better than leaking.
    shutdown();
}

bool GlueBackdrop::update(const GlueSceneState& scene, int width, int height,
                          pipeline::AssetManager* assets,
                          rendering::Renderer* renderer, float deltaTime) {
    if (scene.model.empty() || !assets || !renderer) return false;
    if (width <= 0 || height <= 0) return false;

    // In pixels, and bounded: the login scene fills the window, and a window
    // dragged larger by a few pixels must not rebuild the whole view.
    const int w = std::clamp((width + 31) / 32 * 32, 128, 2048);
    const int h = std::clamp((height + 31) / 32 * 32, 128, 2048);

    if (view_ && (view_->width != w || view_->height != h)) {
        view_->destroy();
        view_.reset();
    }
    if (!view_) {
        view_ = std::make_unique<View>();
        view_->assets = assets;
        if (!view_->build(w, h, renderer)) {
            view_->destroy();
            view_.reset();
            return false;
        }
    }

    // The rect the interface will draw into, which is not the target's size.
    // A change here only re-frames the camera; the target is kept.
    if (view_->drawWidth != width || view_->drawHeight != height) {
        view_->drawWidth = width;
        view_->drawHeight = height;
        if (view_->placed) view_->frameThrough(view_->appliedCamera);
        else view_->camera->setAspectRatio(view_->drawAspect());
    }

    if (view_->loadedPath != scene.model) {
        view_->loadedPath = scene.model;
        view_->loadScene(scene.model, scene);
    }
    view_->applyScene(scene);
    if (!view_->placed) return false;

    // The view-projection is what this renderer culls against. A glue scene is
    // one instance and the camera stands inside it, so this is the camera's own.
    view_->models->update(deltaTime, view_->camera->getPosition(),
                          view_->camera->getProjectionMatrix() *
                              view_->camera->getViewMatrix());
    view_->composite();
    return view_->everComposited;
}

uint64_t GlueBackdrop::textureId() const {
    if (!view_ || !view_->everComposited) return 0;
    // The glow pass writes the scene with its glow already in it, so that is
    // the picture when it ran. When it did not - no glow on this screen, or
    // WOWEE_NO_GLUE_GLOW - the scene target is the picture and nothing else
    // was drawn or paid for.
    if (view_->glowApplied) return reinterpret_cast<uint64_t>(view_->glowTextureId);
    return reinterpret_cast<uint64_t>(view_->imguiTexture);
}

float GlueBackdrop::textureU1() const {
    return view_ ? view_->usedU() : 1.0f;
}

float GlueBackdrop::textureV1() const {
    return view_ ? view_->usedV() : 1.0f;
}

void GlueBackdrop::shutdown() {
    if (view_) view_->destroy();
    view_.reset();
}

} // namespace wowee::ui
