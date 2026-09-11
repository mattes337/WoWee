#include "ui/unit_portrait.hpp"
#include "game/equipment_hash.hpp"

#include "core/logger.hpp"
#include "game/game_handler.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_asset_loader.hpp"
#include "pipeline/m2_loader.hpp"
#include "rendering/camera.hpp"
#include "rendering/character_preview.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/imgui_texture.hpp"
#include "rendering/renderer.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_frame_data.hpp"
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

    std::unique_ptr<rendering::CharacterRenderer> models;
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
    /// The same 1x1 image as a one-layer array, because the renderer's layout
    /// declares the cascade bindings whether or not this pass reads them.
    VkImageView shadowArrayView = VK_NULL_HANDLE;
    VmaAllocation shadowAlloc = VK_NULL_HANDLE;

    VkDescriptorSet imguiTexture = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;
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

    models = std::make_unique<rendering::CharacterRenderer>();
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
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        if (vkCreateImageView(device, &viewCI, nullptr, &shadowArrayView) != VK_SUCCESS) {
            LOG_WARNING("GlueBackdrop: could not create the shadow stand-in array view");
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
        VkDescriptorPoolSize sizes[2]{};
        sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        sizes[0].descriptorCount = 1;
        sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        // Three image bindings in the renderer's per-frame layout now: the
        // single shadow map, the cascade array, and the array again without the
        // comparison sampler.
        sizes[1].descriptorCount = 3;
        VkDescriptorPoolCreateInfo ci{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        ci.maxSets = 1;
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

        VkDescriptorImageInfo shadowArrayImg{};
        shadowArrayImg.imageView = shadowArrayView;
        shadowArrayImg.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet writes[4]{};
        for (uint32_t b = 2; b < 4; ++b) {
            writes[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet = perFrameSet;
            writes[b].dstBinding = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[b].pImageInfo = &shadowArrayImg;
        }
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
        vkUpdateDescriptorSets(device, 4, writes, 0, nullptr);
    }

    camera = std::make_unique<rendering::Camera>();
    camera->setAspectRatio(static_cast<float>(width) / static_cast<float>(height));

    imguiTexture = ImGui_ImplVulkan_AddTexture(target->getSampler(),
                                               target->getColorImageView(),
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    LOG_INFO("GlueBackdrop: view built (", width, "x", height, ")");
    return true;
}

void GlueBackdrop::View::destroy() {
    if (!ctx) return;
    VkDevice device = ctx->getDevice();
    VmaAllocator allocator = ctx->getAllocator();

    // The image is sampled by whatever frames are still in flight.
    vkDeviceWaitIdle(device);

    if (imguiTexture != VK_NULL_HANDLE) rendering::removeImGuiTexture(imguiTexture);
    if (models) { models->shutdown(); models.reset(); }
    camera.reset();
    if (ubo != VK_NULL_HANDLE) rendering::destroy(allocator, ubo, uboAlloc);
    uboMapped = nullptr;
    perFrameSet = VK_NULL_HANDLE;
    if (descPool != VK_NULL_HANDLE) rendering::destroy(device, descPool);
    if (shadowArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, shadowArrayView, nullptr);
        shadowArrayView = VK_NULL_HANDLE;
    }
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

    pipeline::M2Model model;
    if (!pipeline::loadM2WithSkin(*assets, m2Path, model)) {
        LOG_WARNING("GlueBackdrop: no model at ", m2Path);
        return false;
    }

    if (!models->loadModel(model, kBackdropModelId)) {
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
    // A whole scene rather than a figure: no distance culling, no stand mark.
    models->setInstanceSceneModel(instanceId, true);

    // The camera is what decides whether this is drawn at all, so it is asked
    // for after the model is up but before anything says the scene is placed.
    if (!frameThrough(scene.cameraIndex)) return false;
    placed = true;
    return true;
}

bool GlueBackdrop::View::frameThrough(int index) {
    const pipeline::M2Model* model = models ? models->getModelData(kBackdropModelId) : nullptr;
    if (model == nullptr || camera == nullptr) return false;

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
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
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
        models->setInstanceSceneModel(instanceId, true);
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
        models->playAnimation(instanceId, want, true);
        appliedSequence = scene.sequence;
    }
}

void GlueBackdrop::View::composite() {
    if (!ctx || !models || !camera || !target || !target->isValid() || !uboMapped) return;
    if (!placed || instanceId == 0) return;

    // Bone buffers and descriptors, allocated before anything is recorded.
    //
    // For the context's own frame slot, not a fixed one: CharacterRenderer's
    // draw loop reads the slot back out of the context rather than taking the
    // one it was prepared for, so preparing slot 0 every time leaves every
    // frame that lands on slot 1 with no bone descriptor and nothing drawn.
    // Half the frames rendered the scene and half rendered the clear colour,
    // which on a screenshot reads as "the backdrop does not work" about as
    // often as it reads as "it does".
    models->prepareRender(ctx->getCurrentFrame());

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
        models->render(cmd, perFrameSet, *camera);
        target->endPass(cmd);
    });
    everComposited = true;
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

    if (view_->loadedPath != scene.model) {
        view_->loadedPath = scene.model;
        view_->loadScene(scene.model, scene);
    }
    view_->applyScene(scene);
    if (!view_->placed) return false;

    view_->models->update(deltaTime, view_->camera->getPosition());
    view_->composite();
    return view_->everComposited;
}

uint64_t GlueBackdrop::textureId() const {
    if (!view_ || !view_->everComposited) return 0;
    return reinterpret_cast<uint64_t>(view_->imguiTexture);
}

void GlueBackdrop::shutdown() {
    if (view_) view_->destroy();
    view_.reset();
}

} // namespace wowee::ui
