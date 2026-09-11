#pragma once

#include <memory>
#include <string>
#include <cstdint>
#include <vector>
#include <future>
#include <cstddef>
#include <unordered_map>
#include <unordered_set>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "rendering/vk_frame_data.hpp"
#include "rendering/vk_utils.hpp"
#include "rendering/sky_system.hpp"
#include "pipeline/custom_zone_discovery.hpp"

#include "pipeline/grass_biomes.hpp"
#include "pipeline/grass_clearing.hpp"
#include "pipeline/grass_population.hpp"
#include "pipeline/grass_profile.hpp"

namespace wowee {
namespace core { class Window; }
namespace rendering { class VkContext; }
namespace game { class World; class ZoneManager; class GameHandler; }
namespace audio { class AudioCoordinator; }
namespace pipeline { class AssetManager; }

namespace rendering {

class Camera;
class CameraController;
class TerrainRenderer;
class TerrainManager;
class PerformanceHUD;
class WaterRenderer;
class Skybox;
class Celestial;
class StarField;
class Clouds;
class LensFlare;
class Weather;
class Lightning;
class LightingManager;
class SwimEffects;
class MountDust;
class LevelUpEffect;
class ChargeEffect;
class CharacterRenderer;
class WMORenderer;
class M2Renderer;
class Minimap;
namespace world_map { class WorldMapFacade; }
using WorldMap = world_map::WorldMapFacade;
class QuestMarkerRenderer;
class FootprintRenderer;
class CharacterPreview;
class AmdFsr3Runtime;
class SpellVisualSystem;
class PostProcessPipeline;
class SunShafts;
class AnimationController;
class LevelUpEffect;
class ChargeEffect;
class SwimEffects;
class RenderGraph;
class OverlaySystem;
class HiZSystem;
class GrassRenderer;

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool initialize(core::Window* window);
    void shutdown();

    void beginFrame();
    void endFrame();

    void renderWorld(game::World* world, game::GameHandler* gameHandler = nullptr);

    /**
     * Update renderer (camera, etc.)
     */
    void update(float deltaTime);

    /**
     * Load test terrain for debugging
     * @param assetManager Asset manager to load terrain data
     * @param adtPath Path to ADT file (e.g., "World\\Maps\\Azeroth\\Azeroth_32_49.adt")
     */
    bool loadTestTerrain(pipeline::AssetManager* assetManager, const std::string& adtPath);

    /**
     * Initialize all sub-renderers (WMO, M2, Character, terrain, water, minimap, etc.)
     * without loading any ADT tile.  Used by WMO-only maps (dungeons/raids/BGs).
     */
    bool initializeRenderers(pipeline::AssetManager* assetManager, const std::string& mapName);
    /// The map every sub-renderer is looking at.
    ///
    /// initializeRenderers set this on all four when it built them, and it
    /// builds them once - so on every later map change the two the loader
    /// remembered by hand moved and the two it did not stayed on the map the
    /// session started in. The minimap was one of those: in a dungeon it went
    /// on asking for the tiles of wherever the player had logged in, and got
    /// none, which is a minimap with no dungeon on it.
    void setActiveMapName(const std::string& name);

    /**
     * Enable/disable terrain rendering
     */
    void setTerrainEnabled(bool enabled) { terrainEnabled = enabled; }

    /**
     * Enable/disable wireframe mode
     */
    /// Draw the terrain as lines. Remembered until there is a terrain
    /// renderer to tell, so it can be set before a world is loaded.
    void setWireframeMode(bool enabled);
    [[nodiscard]] bool getWireframeMode() const { return wireframeMode_; }
    /// How far the ground may drop in detail: 0 off, 3 furthest. Remembered
    /// for the same reason - the settings are applied once the renderer
    /// exists, and the terrain renderer is made when a world is loaded.
    void setTerrainLodLevel(int level);
    [[nodiscard]] int getTerrainLodLevel() const { return terrainLodLevel_; }



    /**
     * Render performance HUD
     */
    void renderHUD();

    Camera* getCamera() { return camera.get(); }
    CameraController* getCameraController() { return cameraController.get(); }
    TerrainRenderer* getTerrainRenderer() const { return terrainRenderer.get(); }
    TerrainManager* getTerrainManager() const { return terrainManager.get(); }
    PerformanceHUD* getPerformanceHUD() { return performanceHUD.get(); }
    WaterRenderer* getWaterRenderer() const { return waterRenderer.get(); }
    Skybox* getSkybox() const { return skySystem ? skySystem->getSkybox() : nullptr; }
    Celestial* getCelestial() const { return skySystem ? skySystem->getCelestial() : nullptr; }
    StarField* getStarField() const { return skySystem ? skySystem->getStarField() : nullptr; }
    Clouds* getClouds() const { return skySystem ? skySystem->getClouds() : nullptr; }
    LensFlare* getLensFlare() const { return skySystem ? skySystem->getLensFlare() : nullptr; }
    Weather* getWeather() const { return weather.get(); }
    Lightning* getLightning() const { return lightning.get(); }
    CharacterRenderer* getCharacterRenderer() const { return characterRenderer.get(); }
    WMORenderer* getWMORenderer() const { return wmoRenderer.get(); }
    M2Renderer* getM2Renderer() const { return m2Renderer.get(); }
    Minimap* getMinimap() const { return minimap.get(); }
    WorldMap* getWorldMap() const { return worldMap.get(); }
    QuestMarkerRenderer* getQuestMarkerRenderer() const { return questMarkerRenderer.get(); }
    FootprintRenderer* getFootprintRenderer() const { return footprintRenderer.get(); }
    SkySystem* getSkySystem() const { return skySystem.get(); }
    const std::string& getCurrentZoneName() const;
    uint32_t getCurrentZoneId() const;
    /// The area under the player, asked of AreaTable rather than resolved to a
    /// zone first - the world PvP flag is on the subzone. See ZoneManager.
    bool isOnOutdoorPvpObjective() const;
    bool isPlayerIndoors() const { return playerIndoors_; }
    VkContext* getVkContext() const { return vkCtx; }
    VkDescriptorSetLayout getPerFrameSetLayout() const { return perFrameSetLayout; }
    VkRenderPass getShadowRenderPass() const { return shadowRenderPass; }

    // Third-person character follow
    void setCharacterFollow(uint32_t instanceId);
    glm::vec3& getCharacterPosition() { return characterPosition; }
    uint32_t getCharacterInstanceId() const { return characterInstanceId; }
    float getCharacterYaw() const { return characterYaw; }
    void setCharacterYaw(float yawDeg) { characterYaw = yawDeg; }

    // Screenshot capture - copies swapchain image to PNG file
    bool captureScreenshot(const std::string& outputPath);

    // Spell visual effects (SMSG_PLAY_SPELL_VISUAL / SMSG_PLAY_SPELL_IMPACT)
    // Delegates to SpellVisualSystem (owned by Renderer)
    SpellVisualSystem* getSpellVisualSystem() const { return spellVisualSystem_.get(); }

    // Combat visual state (compound: resets AnimationController + SpellVisualSystem)
    void resetCombatVisualState();

    // Sub-system accessors (§4.2)
    AnimationController* getAnimationController() const { return animationController_.get(); }
    LevelUpEffect* getLevelUpEffect() const { return levelUpEffect.get(); }
    ChargeEffect* getChargeEffect() const { return chargeEffect.get(); }
    SwimEffects* getSwimEffects() const { return swimEffects.get(); }

    // Selection circle for targeted entity
    void setSelectionCircle(const glm::vec3& pos, float radius, const glm::vec3& color);
    void clearSelectionCircle();

    // CPU timing stats (milliseconds, last frame).
    double getLastUpdateMs() const { return lastUpdateMs; }
    double getLastRenderMs() const { return lastRenderMs; }
    double getLastCameraUpdateMs() const { return lastCameraUpdateMs; }
    double getLastTerrainRenderMs() const { return lastTerrainRenderMs; }
    double getLastWMORenderMs() const { return lastWMORenderMs; }
    double getLastM2RenderMs() const { return lastM2RenderMs; }
    // Audio coordinator - owned by Application, set via setAudioCoordinator().
    void setAudioCoordinator(audio::AudioCoordinator* ac) { audioCoordinator_ = ac; }
    audio::AudioCoordinator* getAudioCoordinator() { return audioCoordinator_; }
    game::ZoneManager* getZoneManager() { return zoneManager.get(); }
    LightingManager* getLightingManager() { return lightingManager.get(); }

    const std::vector<pipeline::CustomZoneInfo>& getCustomZones() const { return customZones_; }

private:
    // True when water is drawn in the scene continuation pass rather than in
    // the scene pass itself (see renderWorld).
    bool waterDrawsInContinuePass() const;

    /// Ghost tint, brightness and the minimap, in that order, at the end of
    /// the scene pass. The threaded and single-threaded paths both finish this
    /// way and differ only in which command buffer they are recording into.
    /// The underwater tint and its waterline. One implementation, called
    /// from both the parallel and the fallback recording paths, which had
    /// carried different ones.
    void renderUnderwaterOverlay(VkCommandBuffer cmd);
    void renderPostSceneOverlays(VkCommandBuffer cmd, game::GameHandler* gameHandler);
    void renderMinimapOverlay(VkCommandBuffer cmd, game::GameHandler* gameHandler);

    /// Point the swim spray at whichever pass the water ends up drawing in, so
    /// it can be recorded after the water rather than under it. Must run before
    /// the spray's pipelines are built, and again whenever they are rebuilt.
    void syncSwimEffectsTargetPass();
    /// syncSwimEffectsTargetPass, then rebuild the spray or the minimap if the
    /// answer moved either of them. Runs from beginFrame every frame, once the
    /// post-process target for the frame is settled; free when nothing moved.
    void refreshSwimEffectsPass();

    /// True when the spray's pipelines were built for the water continuation
    /// pass. Draw sites test this rather than waterDrawsInContinuePass() so a
    /// mid-run mode change cannot record a pipeline into an incompatible pass.
    bool swimEffectsDrawWithWater_ = false;
    /// Whether the minimap follows water out of the scene pass, for the same
    /// reason the spray does - see syncSwimEffectsTargetPass.
    bool minimapDrawsWithWater_ = false;

    void runDeferredWorldInitStep(float deltaTime);

    core::Window* window = nullptr;
    std::unique_ptr<Camera> camera;
    std::unique_ptr<CameraController> cameraController;
    std::unique_ptr<TerrainRenderer> terrainRenderer;
    std::unique_ptr<TerrainManager> terrainManager;
    std::unique_ptr<PerformanceHUD> performanceHUD;
    std::unique_ptr<WaterRenderer> waterRenderer;
    std::unique_ptr<Skybox> skybox;
    std::unique_ptr<Celestial> celestial;
    std::unique_ptr<StarField> starField;
    std::unique_ptr<Clouds> clouds;
    std::unique_ptr<LensFlare> lensFlare;
    std::unique_ptr<Weather> weather;
    std::unique_ptr<Lightning> lightning;
    std::unique_ptr<LightingManager> lightingManager;
    std::unique_ptr<SkySystem> skySystem;  // Coordinator for sky rendering
    std::unique_ptr<SwimEffects> swimEffects;
    std::unique_ptr<MountDust> mountDust;
    std::unique_ptr<LevelUpEffect> levelUpEffect;
    std::unique_ptr<ChargeEffect> chargeEffect;
    std::unique_ptr<CharacterRenderer> characterRenderer;
    std::unique_ptr<WMORenderer> wmoRenderer;
    std::unique_ptr<M2Renderer> m2Renderer;
    std::unique_ptr<M2Renderer> skyboxModelRenderer_;
    /// The last zone a terrain chunk actually named. Many chunks carry an area
    /// id of zero, and a tile being loaded carries none at all, so the lookup
    /// answers "do not know" often - and answering from somewhere else instead
    /// made the zone flip at chunk boundaries while walking. Mutable because
    /// getCurrentZoneId() is const and this is a cache of what it last learnt.
    mutable uint32_t lastResolvedZoneId_ = 0;
    /// The map that answer belongs to, so it is dropped on a
    /// continent change rather than held across one.
    mutable uint32_t lastResolvedZoneMapId_ = 0xFFFFFFFFu;

    std::string skyboxModelPath_;
    uint32_t skyboxModelInstanceId_ = 0;
    /// Sky paths that did not resolve to a usable model. The swap is atomic -
    /// the old sky is kept when a new one cannot be loaded - so without this
    /// the failing path would be read off disk again on every frame it was
    /// active.
    std::unordered_set<std::string> failedSkyboxPaths_;
    std::unique_ptr<Minimap> minimap;
    std::unique_ptr<WorldMap> worldMap;
    std::unique_ptr<QuestMarkerRenderer> questMarkerRenderer;
    std::unique_ptr<FootprintRenderer> footprintRenderer;
    audio::AudioCoordinator* audioCoordinator_ = nullptr;  // Owned by Application
    std::unique_ptr<AnimationController> animationController_;  // §4.2
    std::unique_ptr<game::ZoneManager> zoneManager;
    // Shadow mapping (Vulkan)
    /// The shadow map is square and this is its side, chosen before the
    /// per-frame resources are built and not changed after.
    ///
    /// Deliberately not live. Those resources are created once at start-up and
    /// destroyed at shutdown - they are not part of the swapchain-resize path,
    /// so rebuilding them mid-session would be a new and unexercised one, and
    /// this renderer has lost a device to exactly that before. The setting is
    /// marked as needing a restart in the panel instead, which is what the
    /// original client does for the settings it cannot change live.
    uint32_t SHADOW_MAP_SIZE = 4096;
    void setShadowMapSize(uint32_t side) {
        // Powers of two between 512 and 4096: the quality slider has five
        // steps and these are they.
        SHADOW_MAP_SIZE = std::clamp(side, 512u, 4096u);
    }
    // Per-frame shadow resources: each in-flight frame has its own depth image and
    // framebuffer so that frame N's shadow read and frame N+1's shadow write don't
    // race on the same image across concurrent GPU submissions.
    // Array size must match MAX_FRAMES (= 2, defined in the private section below).
    /// The most cascades the depth array is ever allocated with, and the
    /// length of every per-cascade array below. Four is what `shadowSplits`
    /// and `cascadeMatrix[4]` in the per-frame block hold.
    static constexpr uint32_t kMaxShadowCascades = 4;
    VkImage shadowDepthImage[2] = {};
    VmaAllocation shadowDepthAlloc[2] = {};
    /// A 2D view of layer 0. What binding 1 - the single map every lit shader
    /// has always sampled - points at, so the one-cascade path is untouched.
    VkImageView shadowDepthView[2] = {};
    /// One single-layer view per cascade, because a framebuffer renders into
    /// one layer at a time.
    VkImageView shadowLayerView[2][kMaxShadowCascades] = {};
    /// The whole array, for binding 2 (comparison) and binding 3 (plain, which
    /// is the only way PCSS can read a depth rather than compare against it).
    VkImageView shadowArrayView[2] = {};
    VkSampler shadowSampler = VK_NULL_HANDLE;
    /// The same filtering without the compare, for the PCSS blocker search.
    VkSampler shadowDepthSampler = VK_NULL_HANDLE;
    VkRenderPass shadowRenderPass = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer[2][kMaxShadowCascades] = {};
    VkImageLayout shadowDepthLayout_[2] = {};
    glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
    glm::vec3 shadowCenter = glm::vec3(0.0f);
    bool shadowCenterInitialized = false;
    bool shadowsEnabled = true;
    /// How many cascades the player asked for, 1 to 4. The number actually
    /// drawn is this or zero - see effectiveShadowCascades().
    int shadowCascades_ = 1;
    /// How many layers the depth array was actually allocated with. Changing
    /// the setting reallocates, between frames.
    uint32_t shadowCascadeLayers_ = 1;
    /// The side of one cascade, which is not SHADOW_MAP_SIZE once there is
    /// more than one of them - see shadowCascadeSide().
    uint32_t shadowCascadeSide_ = 4096;
    int pendingShadowCascades_ = 0;
    bool shadowCascadeChangePending_ = false;
    /// Set when shadows have just been switched off, so the map is cleared to
    /// "nothing occludes" and left in the layout its readers expect exactly
    /// once, rather than every frame for as long as they stay off.
    bool shadowOffFlushPending_ = false;
    int shadowOffFlushFrames_ = 0;
    int shadowFilter_ = 0;
    float shadowLightSize_ = 1.5f;
    glm::mat4 cascadeMatrices_[kMaxShadowCascades] = {};
    glm::vec3 cascadeCenters_[kMaxShadowCascades] = {};
    float cascadeRadii_[kMaxShadowCascades] = {};
    float cascadeSplits_[kMaxShadowCascades] = {};
    float shadowDistance_ = 300.0f;  // Shadow frustum half-extent (default: 300 units)
    float viewDistance_ = 1200.0f;
    bool wireframeMode_ = false;
    int terrainLodLevel_ = 0;
    bool sharpStars_ = true;
    float diagTerrainFurthest_ = -1.0f;
    float diagM2Furthest_ = -1.0f;


public:
    // Character preview registration (for off-screen composite pass)
    void registerPreview(CharacterPreview* preview);
    void unregisterPreview(CharacterPreview* preview);

    /// Turning shadows off used to lose the device within a second, so this
    /// held the value on and the setting was dead. What actually did it was
    /// the pass returning before its barriers: the depth image stayed in
    /// whatever layout it was last left in while every lit descriptor set went
    /// on naming it as SHADER_READ_ONLY_OPTIMAL, and a driver reading an
    /// attachment-layout image through a sampler is the shape of fault that
    /// takes a device down rather than drawing something wrong. Off now means
    /// the map is cleared to "nothing occludes" and transitioned once, on the
    /// frame the switch flips, after which the pass really is skipped and
    /// shadowParams.x is 0 so no shader samples it at all.
    void setShadowsEnabled(bool enabled);
    bool areShadowsEnabled() const { return shadowsEnabled; }

    /// How many cascades the sun shadow is split into, 1 to 4.
    ///
    /// 1 is the single orthographic map the client always drew, and it is the
    /// off value in every sense: the shaders fold the cascaded path away, the
    /// depth array is one layer, and that layer is SHADOW_MAP_SIZE a side.
    /// Above 1 the per-cascade side is capped at 2048 so four cascades cost
    /// the memory one 4096 map already did.
    ///
    /// The count is a specialization constant, so a change rebuilds the four
    /// lit renderers' pipelines and reallocates the depth array. Both are
    /// queued for between frames, exactly as an MSAA change is.
    void setShadowCascades(int count);
    [[nodiscard]] int getShadowCascades() const { return shadowCascades_; }

    /// 0 = the 3x3 PCF that shipped, 1 = 16-tap rotated Poisson, 2 = PCSS on
    /// the two near cascades. Only read on the cascaded path; a specialization
    /// constant, so it rebuilds the lit pipelines the same way.
    void setShadowFilter(int filter);
    [[nodiscard]] int getShadowFilter() const { return shadowFilter_; }

    /// How wide the sun is, in yards, for the PCSS penumbra. Live: a per-frame
    /// value, not a variant.
    void setShadowLightSize(float yards) { shadowLightSize_ = glm::clamp(yards, 0.5f, 5.0f); }
    [[nodiscard]] float getShadowLightSize() const { return shadowLightSize_; }
    void setShadowDistance(float dist) { shadowDistance_ = glm::clamp(dist, 40.0f, 500.0f); }
    float getShadowDistance() const { return shadowDistance_; }
    void setViewDistance(float distance);
    float getViewDistance() const { return viewDistance_; }
    /// Draw the client's own point stars in place of the sky model's baked
    /// star layer, which is a 256x256 compressed texture stretched across the
    /// whole dome. See Renderer::setSharpStars.
    /// WOWEE_VIEW_DIAG=1: one line naming how far terrain and doodads each
    /// actually drew, so the two can be compared rather than reasoned about.
    void logViewDistanceDiag();
    void setSharpStars(bool enabled);
    bool areSharpStars() const { return sharpStars_; }
    int getTerrainLoadRadius() const;
    int getTerrainUnloadRadius() const { return getTerrainLoadRadius() + 3; }
    void setMsaaSamples(VkSampleCountFlagBits samples);

    // Post-process pipeline API - delegates to PostProcessPipeline (§4.3)
    PostProcessPipeline* getPostProcessPipeline() const;
    void setFSREnabled(bool enabled);
    void setFSR2Enabled(bool enabled);

    void setWaterRefractionEnabled(bool enabled);

    /// 0 = the linear start/end ramp the client has always had, 1 = exponential
    /// height fog with aerial perspective.
    ///
    /// The model is a specialization constant, so changing it means rebuilding
    /// the four lit renderers' pipelines. That is queued for between frames,
    /// exactly as an MSAA change is: destroying a pipeline a recorded command
    /// buffer still names is how this renderer has lost a device before.
    void setFogModel(int model);
    [[nodiscard]] int getFogModel() const { return fogModel_; }

    /// Screen-space sun shafts, and how far they are turned up.
    void setSunShaftsEnabled(bool enabled);
    [[nodiscard]] bool areSunShaftsEnabled() const;
    void setSunShaftStrength(float strength);
    [[nodiscard]] float getSunShaftStrength() const;

    /// Which surfaces read a generated normal map: 0 = the buildings and
    /// characters that always did, 1 = doodads and the ground as well.
    ///
    /// A specialization constant, like the fog model, so moving it rebuilds the
    /// four lit renderers' pipelines between frames. It also decides whether
    /// the doodad and terrain caches ask for maps at all: off, no worker and no
    /// disk is touched.
    void setNormalMapScope(int scope);
    [[nodiscard]] int getNormalMapScope() const { return normalMapScope_; }
    /// Where the generated maps are kept: `<data>/generated/<expansion>/normals`.
    [[nodiscard]] std::string normalMapCacheDir() const;

    /// How strongly the fog carries the sun's colour when looking toward it.
    /// Live: it is a per-frame value, not a variant.
    void setFogAerialStrength(float strength) {
        fogAerialStrength_ = glm::clamp(strength, 0.0f, 1.0f);
    }
    [[nodiscard]] float getFogAerialStrength() const { return fogAerialStrength_; }

private:
    void applyMsaaChange();
    /// Rebuild the lit renderers' pipelines against the current shader variant.
    /// Between frames only - see setFogModel.
    void applyShaderFeatureChange();
    bool shaderFeatureChangePending_ = false;
    int fogModel_ = 0;
    float fogAerialStrength_ = 0.6f;
    /// Off until something says otherwise: the shader constant's default is
    /// what the client did before phase 01, and so is this.
    int normalMapScope_ = 0;
    bool ensureSkyboxModel();
    VkSampleCountFlagBits pendingMsaaSamples_ = VK_SAMPLE_COUNT_1_BIT;
    bool msaaChangePending_ = false;
    void renderShadowPass();
    glm::mat4 computeLightSpaceMatrix();
    /// Fit one orthographic box per cascade to its slice of the view frustum,
    /// snapped to its own texel grid. Fills cascadeMatrices_, cascadeCenters_,
    /// cascadeRadii_ and cascadeSplits_, and answers cascade 0's matrix so the
    /// single-map path and the cascaded path agree about the nearest one.
    glm::mat4 computeCascadeMatrices();
    /// How many cascades are actually drawn this frame: the setting, or zero
    /// when shadows are off.
    [[nodiscard]] int effectiveShadowCascades() const {
        return shadowsEnabled ? shadowCascades_ : 0;
    }
    /// The side of one cascade's map. Four 4096 maps per in-flight frame is
    /// half a gigabyte of depth; capping the multi-cascade side at 2048 puts a
    /// four-cascade set at the memory the single 4096 map already used.
    [[nodiscard]] uint32_t shadowCascadeSide() const {
        return (shadowCascades_ > 1) ? std::min(SHADOW_MAP_SIZE, 2048u) : SHADOW_MAP_SIZE;
    }
    bool createShadowResources();
    void destroyShadowResources();
    /// Point bindings 1, 2 and 3 of every per-frame set at the current shadow
    /// views. Called when the sets are made and again whenever the depth array
    /// is reallocated.
    void writeShadowDescriptors();
    void applyShadowCascadeChange();

    std::vector<pipeline::CustomZoneInfo> customZones_;
    pipeline::AssetManager* cachedAssetManager = nullptr;

    // Spell visual effects - owned SpellVisualSystem (extracted from Renderer §4.4)
    std::unique_ptr<SpellVisualSystem> spellVisualSystem_;

    // Post-process pipeline - owns all FSR/FXAA/FSR2 state (extracted §4.3)
    std::unique_ptr<PostProcessPipeline> postProcessPipeline_;
    /// S4. Owned here rather than by PostProcessPipeline because it runs on
    /// every path, including the one where no post pass exists at all - see the
    /// header of sun_shafts.hpp.
    std::unique_ptr<SunShafts> sunShafts_;

    bool playerIndoors_ = false;  // Cached WMO inside state for macro conditionals
    bool deferredWorldInitEnabled_ = true;
    bool deferredWorldInitPending_ = false;
    uint8_t deferredWorldInitStage_ = 0;
    float deferredWorldInitCooldown_ = 0.0f;

    // Third-person character state
    glm::vec3 characterPosition = glm::vec3(0.0f);
    uint32_t characterInstanceId = 0;
    float characterYaw = 0.0f;

    // Where the player was, for shaders that react to them moving through the
    // world. playerWakePos_ chases characterPosition with a fixed time
    // constant; the lag is what gives brushed-past foliage its springback.
    glm::vec3 playerWakePos_ = glm::vec3(0.0f);
    glm::vec3 prevPlayerPos_ = glm::vec3(0.0f);
    float playerSpeed_ = 0.0f;
    bool playerMotionTracked_ = false;



    // Selection circle + overlay rendering (owned by OverlaySystem)
    std::unique_ptr<OverlaySystem> overlaySystem_;



    // Vulkan frame state
    VkContext* vkCtx = nullptr;
    VkCommandBuffer currentCmd = VK_NULL_HANDLE;
    uint32_t currentImageIndex = 0;

    // Per-frame UBO + descriptors (set 0)
    static constexpr uint32_t MAX_FRAMES = 2;
    VkDescriptorSetLayout perFrameSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool sceneDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet perFrameDescSets[MAX_FRAMES] = {};
    VkBuffer perFrameUBOs[MAX_FRAMES] = {};
    VmaAllocation perFrameUBOAllocs[MAX_FRAMES] = {};
    void* perFrameUBOMapped[MAX_FRAMES] = {};
    GPUPerFrameData currentFrameData{};
    float globalTime = 0.0f;

    // Per-frame reflection UBO (mirrors camera for planar reflections)
    VkBuffer reflPerFrameUBO = VK_NULL_HANDLE;
    VmaAllocation reflPerFrameUBOAlloc = VK_NULL_HANDLE;
    void* reflPerFrameUBOMapped = nullptr;
    VkDescriptorSet reflPerFrameDescSet[MAX_FRAMES] = {};

    bool createPerFrameResources();
    void destroyPerFrameResources();
    void updatePerFrameUBO();
    void setupWater1xPass();
    void renderReflectionPass();

    // ── Multithreaded secondary command buffer recording ──
    // Indices into secondaryCmds_ arrays
    static constexpr uint32_t SEC_SKY       = 0;  // sky (main thread)
    static constexpr uint32_t SEC_TERRAIN   = 1;  // terrain (worker 0)
    static constexpr uint32_t SEC_WMO       = 2;  // WMO (worker 1)
    static constexpr uint32_t SEC_SELECTION = 3;  // selection circle (main thread)
    static constexpr uint32_t SEC_CHARS     = 4;  // characters (worker 2)
    static constexpr uint32_t SEC_M2        = 5;  // M2 + particles + glow (worker 3)
    static constexpr uint32_t SEC_POST      = 6;  // water + weather + effects (worker 4)
    static constexpr uint32_t SEC_IMGUI     = 7;  // ImGui (main thread, non-FSR only)
    static constexpr uint32_t NUM_SECONDARIES = 8;
    static constexpr uint32_t NUM_WORKERS = 5;

    // Per-worker command pools (thread-safe: one pool per thread)
    VkCommandPool workerCmdPools_[NUM_WORKERS] = {};
    // Main-thread command pool for its secondary buffers
    VkCommandPool mainSecondaryCmdPool_ = VK_NULL_HANDLE;
    // Pre-allocated secondary command buffers [secondaryIndex][frameInFlight]
    VkCommandBuffer secondaryCmds_[NUM_SECONDARIES][MAX_FRAMES] = {};

    bool parallelRecordingEnabled_ = false;  // set true after pools/buffers created
    float lastDeltaTime_ = 0.0f;           // cached for post-process pipeline
    bool createSecondaryCommandResources();
    void destroySecondaryCommandResources();
    VkCommandBuffer beginSecondary(uint32_t secondaryIndex);
    void setSecondaryViewportScissor(VkCommandBuffer cmd);

    // Cached render pass state for secondary buffer inheritance
    VkRenderPass activeRenderPass_ = VK_NULL_HANDLE;
    VkFramebuffer activeFramebuffer_ = VK_NULL_HANDLE;
    VkExtent2D activeRenderExtent_ = {.width = 0, .height = 0};

    // Active character previews for off-screen rendering
    std::vector<CharacterPreview*> activePreviews_;

    bool terrainEnabled = true;
    bool terrainLoaded = false;

    bool ghostMode_ = false;  // set each frame from gameHandler->isPlayerGhost()

    // Render Graph - declarative pass ordering with automatic barriers
    std::unique_ptr<RenderGraph> renderGraph_;
    void buildFrameGraph(game::GameHandler* gameHandler);

    // HiZ occlusion culling - builds depth pyramid each frame
    std::unique_ptr<HiZSystem> hizSystem_;

    // GPU-driven grass: compute cull with atomic compaction feeding an
    // indirect draw, over a population generated from terrain suitability.
    std::unique_ptr<GrassRenderer> grassRenderer_;
    // Where the live population was generated for. Rebuilt when the player
    // leaves it; the generator's lattice is world-anchored, so a rebuild
    // reproduces every blade that is still in range rather than reshuffling.
    glm::vec3 grassWindowCenter_{0.0f};
    bool grassWindowValid_ = false;
    // The window being generated, a bounded number of lattice cells per
    // frame. Small windows still finish inside one; the ones the distance
    // slider allows take as many frames as they take, and upload when done.
    pipeline::GrassPopulationBuilder grassBuilder_;
    glm::vec3 grassBuildCenter_{0.0f};
    // Clearings around placed WMOs and props for the window being built,
    // gathered once per rebuild from the renderers that own the instances.
    pipeline::GrassClearingField grassClearing_;
    void updateGrassPopulation();
public:
    /** Grass density and height, as fractions of the generator's defaults.
     * Set from the settings panel; either changing rebuilds the field. */
    void setGrassScales(float density, float height);
    /** How far out grass draws, in yards. Density thins with distance past
     * the near field, so range costs blades logarithmically, not by area. */
    void setGrassDistance(float yards);
    /** Turn grass on or off. Off is the default: it is new, it costs
     * generation time on the main thread, and turning it off has to release
     * what it was holding rather than merely stop drawing. */
    void setGrassEnabled(bool enabled);
private:
    bool grassEnabled_ = false;
    float grassDensityScale_ = 1.0f;
    float grassHeightScale_ = 1.0f;
    float grassDistance_ = 150.0f;
    // (biome, effectId) -> index into the profile table, built as effects are
    // met. Grass profiles are a blend of five categories crossed with the
    // biome overrides, so distinct ones stay far fewer than the hundreds of
    // ground effects that map onto them.
    std::unordered_map<uint64_t, uint32_t> grassProfileIndex_;
    std::vector<pipeline::GrassProfile> grassProfiles_;
    uint32_t grassProfileFor(uint32_t effectId, uint32_t areaId);
    // The per-zone look table from assets/grass_biomes.json, loaded on first
    // use, and a memo of which biome each area resolved to - resolution walks
    // AreaTable parentage and runs per blade sample without it.
    pipeline::GrassBiomeSet grassBiomes_;
    bool grassBiomesLoaded_ = false;
    std::unordered_map<uint32_t, uint32_t> grassBiomeForArea_;
    // Session totals, reported once at shutdown. The log is bounded and the
    // per-rebuild lines rotate out of it whenever the player stands still for
    // a minute, so the tail is the only place a "check the log" can rely on.
    uint32_t grassRebuilds_ = 0;
    size_t grassLastCount_ = 0;
    double grassWorstGenerateMs_ = 0.0;

    // CPU timing stats (last frame/update).
    double lastUpdateMs = 0.0;
    double lastRenderMs = 0.0;
    double lastCameraUpdateMs = 0.0;
    double lastTerrainRenderMs = 0.0;
    double lastWMORenderMs = 0.0;
    double lastM2RenderMs = 0.0;
};

} // namespace rendering
} // namespace wowee
