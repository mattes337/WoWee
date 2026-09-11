#include "rendering/character_preview.hpp"
#include "rendering/imgui_texture.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/animation/animation_ids.hpp"
#include "rendering/vk_render_target.hpp"
#include "rendering/vk_texture.hpp"
#include "rendering/vk_context.hpp"
#include "rendering/vk_frame_data.hpp"
#include "rendering/camera.hpp"
#include "rendering/renderer.hpp"
#include "pipeline/asset_manager.hpp"
#include "pipeline/m2_loader.hpp"
#include "pipeline/dbc_loader.hpp"
#include "pipeline/char_sections.hpp"
#include "pipeline/dbc_layout.hpp"
#include "core/appearance_composer.hpp"
#include "core/geoset_rules.hpp"
#include "pipeline/item_textures.hpp"
#include "pipeline/m2_asset_loader.hpp"
#include "core/logger.hpp"
#include "core/application.hpp"
#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <cstring>

namespace wowee {
namespace rendering {

namespace {

bool isFiniteVec3(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void frameCameraForModelBounds(Camera& camera, const glm::vec3& boundMin, const glm::vec3& boundMax) {
    if (!isFiniteVec3(boundMin) || !isFiniteVec3(boundMax)) {
        return;
    }

    const glm::vec3 extent = boundMax - boundMin;
    if (extent.z <= 0.0f) {
        return;
    }

    const float fovY = glm::radians(camera.getFovDegrees());
    const float tanHalfY = std::tan(fovY * 0.5f);
    if (!std::isfinite(tanHalfY) || tanHalfY <= 0.0f) {
        return;
    }

    const float tanHalfX = tanHalfY * std::max(camera.getAspectRatio(), 0.1f);
    const float height = std::max(extent.z, 0.1f);
    const float width = std::max(std::max(extent.x, extent.y), 0.1f);
    const float margin = 1.50f;
    const float distanceForHeight = (height * margin) / (2.0f * tanHalfY);
    const float distanceForWidth = (width * margin) / (2.0f * tanHalfX);
    const float distance = std::max({4.5f, distanceForHeight, distanceForWidth});
    const float centerZ = (boundMin.z + boundMax.z) * 0.5f;

    camera.setPosition(glm::vec3(0.0f, distance, centerZ));
    camera.setRotation(270.0f, 0.0f);
}

} // namespace

CharacterPreview::CharacterPreview() = default;

CharacterPreview::~CharacterPreview() {
    shutdown();
}

void CharacterPreview::ensureAppearanceGeosetsLoaded() {
    if (appearanceGeosetsLoaded_ || !assetManager_) {
        return;
    }

    appearanceGeosetsLoaded_ = true;
    hairGeosetMap_.clear();
    facialHairGeosetMap_.clear();

    // CharHairGeosets.dbc maps (race, sex, hairStyleId) to the group-0
    // scalp/hair submesh. Activating every group-0 submesh draws all hair
    // variants at once, which shows up as flickering magenta patches.
    if (auto chg = assetManager_->loadDBC("CharHairGeosets.dbc"); chg && chg->isLoaded()) {
        const auto* chgL = pipeline::getActiveDBCLayout()
            ? pipeline::getActiveDBCLayout()->getLayout("CharHairGeosets") : nullptr;
        for (uint32_t i = 0; i < chg->getRecordCount(); i++) {
            uint32_t raceId = chg->getUInt32(i, chgL ? (*chgL)["RaceID"] : 1);
            uint32_t sexId = chg->getUInt32(i, chgL ? (*chgL)["SexID"] : 2);
            uint32_t variation = chg->getUInt32(i, chgL ? (*chgL)["Variation"] : 3);
            uint32_t geosetId = chg->getUInt32(i, chgL ? (*chgL)["GeosetID"] : 4);
            const bool useDefaultScalp = chg->getFieldCount() > 5 && chg->getUInt32(i, 5) != 0;
            const uint32_t key = core::appearanceKey(static_cast<uint8_t>(raceId),
                                                    static_cast<uint8_t>(sexId),
                                                    static_cast<uint8_t>(variation));
            hairGeosetMap_[key] = static_cast<uint16_t>(useDefaultScalp ? 1 : geosetId);
        }
        LOG_INFO("CharacterPreview: loaded ", hairGeosetMap_.size(), " hair geoset mappings");
    }

    if (auto cfh = assetManager_->loadDBC("CharacterFacialHairStyles.dbc"); cfh && cfh->isLoaded()) {
        const auto* cfhL = pipeline::getActiveDBCLayout()
            ? pipeline::getActiveDBCLayout()->getLayout("CharacterFacialHairStyles") : nullptr;
        const auto fhF = pipeline::detectFacialHairFields(cfh.get(), cfhL);
        for (uint32_t i = 0; i < cfh->getRecordCount(); i++) {
            uint32_t raceId = cfh->getUInt32(i, cfhL ? (*cfhL)["RaceID"] : 0);
            uint32_t sexId = cfh->getUInt32(i, cfhL ? (*cfhL)["SexID"] : 1);
            uint32_t variation = cfh->getUInt32(i, cfhL ? (*cfhL)["Variation"] : 2);
            const uint32_t key = core::appearanceKey(static_cast<uint8_t>(raceId),
                                                    static_cast<uint8_t>(sexId),
                                                    static_cast<uint8_t>(variation));

            FacialHairGeosets geosets;
            // Whichever columns this copy of the DBC keeps them in - see
            // detectFacialHairFields, and the same read in EntitySpawner.
            geosets.geoset100 = static_cast<uint16_t>(cfh->getUInt32(i, fhF.geoset100));
            geosets.geoset300 = static_cast<uint16_t>(cfh->getUInt32(i, fhF.geoset300));
            geosets.geoset200 = static_cast<uint16_t>(cfh->getUInt32(i, fhF.geoset200));
            facialHairGeosetMap_[key] = geosets;
        }
        LOG_INFO("CharacterPreview: loaded ", facialHairGeosetMap_.size(), " facial hair geoset mappings");
    }
}

uint16_t CharacterPreview::selectedHairScalpGeoset() const {
    const uint8_t raceId = static_cast<uint8_t>(race_);
    const uint8_t sexId = (gender_ == game::Gender::FEMALE ||
                           (gender_ == game::Gender::NONBINARY && useFemaleModel_)) ? 1u : 0u;
    const uint32_t key = core::appearanceKey(raceId, sexId, static_cast<uint8_t>(hairStyle_));

    auto it = hairGeosetMap_.find(key);
    if (it != hairGeosetMap_.end() && it->second > 0) {
        return it->second;
    }

    // Last-resort heuristic for incomplete data sets. The DBC path above is
    // expected for real clients and is much more reliable than this fallback.
    return static_cast<uint16_t>(std::max<uint8_t>(hairStyle_ + 1, 1));
}

std::unordered_set<uint16_t> CharacterPreview::buildBaseGeosets() {
    ensureAppearanceGeosetsLoaded();

    const uint8_t raceId = static_cast<uint8_t>(race_);
    const uint8_t sexId = (gender_ == game::Gender::FEMALE ||
                           (gender_ == game::Gender::NONBINARY && useFemaleModel_)) ? 1u : 0u;
    const uint16_t selectedHairScalp = selectedHairScalpGeoset();

    uint16_t facial100 = 1, facial200 = 1, facial300 = 1;
    auto itFacial = facialHairGeosetMap_.find(
        core::appearanceKey(raceId, sexId, static_cast<uint8_t>(facialHair_)));
    if (itFacial != facialHairGeosetMap_.end()) {
        facial100 = itFacial->second.geoset100;
        facial200 = itFacial->second.geoset200;
        facial300 = itFacial->second.geoset300;
    }

    // The same bare set the player gets. This used to be its own list and had
    // drifted: it named one of the two feet variants, so an HD model spelling
    // its feet the other way stood in the portrait without them, and it named
    // the no-cloak panel, which the HD models do not carry.
    std::unordered_set<uint16_t> activeGeosets =
        core::bareCharacterGeosets(selectedHairScalp, facial100, facial200, facial300, raceId);

    return activeGeosets;
}

bool CharacterPreview::initialize(pipeline::AssetManager* am, int width, int height) {
    assetManager_ = am;
    // If already initialized with valid resources, reuse them.
    // This avoids destroying GPU resources that may still be referenced by
    // an in-flight command buffer (compositePass recorded earlier this frame).
    if (scene_ && scene_->isBuilt() && charRenderer_ && camera_) {
        // Mark model as not loaded - loadCharacter() will handle instance cleanup
        modelLoaded_ = false;
        return true;
    }

    // Only where the view is actually being built. The render target, the
    // camera's aspect ratio and the composite are all sized from these, so
    // taking a new size while keeping a target built at the old one would put
    // every one of the three out of step with the image they are drawing into.
    if (width > 0) fboWidth_ = width;
    if (height > 0) fboHeight_ = height;

    auto* appRenderer = core::Application::getInstance().getRenderer();
    vkCtx_ = appRenderer ? appRenderer->getVkContext() : nullptr;
    VkDescriptorSetLayout perFrameLayout = appRenderer ? appRenderer->getPerFrameSetLayout() : VK_NULL_HANDLE;

    if (!vkCtx_ || perFrameLayout == VK_NULL_HANDLE) {
        LOG_ERROR("CharacterPreview: no VkContext or perFrameLayout available");
        return false;
    }

    // The view is a GlueScene with nothing on show: the same target, per-frame
    // blocks and glow passes an authored scene gets, so a character stood in
    // one is drawn exactly as the scene is.
    scene_ = std::make_unique<GlueScene>();
    if (!scene_->build(fboWidth_, fboHeight_, appRenderer, am)) {
        LOG_ERROR("CharacterPreview: failed to create off-screen render target");
        scene_.reset();
        return false;
    }
    scene_->setClearColor(transparentBackground_ ? glm::vec4(0.0f)
                                                 : glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));

    // Initialize CharacterRenderer with the view's render pass
    charRenderer_ = std::make_unique<CharacterRenderer>();
    if (!charRenderer_->initialize(vkCtx_, perFrameLayout, am, scene_->renderPass(),
                                   scene_->sampleCount())) {
        LOG_ERROR("CharacterPreview: failed to initialize CharacterRenderer");
        return false;
    }

    // Configure lighting for character preview
    // Use distant fog to avoid clipping, enable shadows for visual depth
    charRenderer_->setFog(glm::vec3(0.05f, 0.05f, 0.1f), 9999.0f, 10000.0f);

    camera_ = std::make_unique<Camera>();
    // Portrait-style camera: WoW Z-up coordinate system
    // Model at origin, camera positioned along +Y looking toward -Y
    camera_->setFov(30.0f);
    camera_->setAspectRatio(static_cast<float>(fboWidth_) / static_cast<float>(fboHeight_));
    // Pull camera back far enough to see full body + head with margin
    camera_->setPosition(glm::vec3(0.0f, 4.5f, 0.9f));
    camera_->setRotation(270.0f, 0.0f);

    LOG_INFO("CharacterPreview initialized (", fboWidth_, "x", fboHeight_, ")");
    return true;
}

void CharacterPreview::shutdown() {
    // Unregister from renderer before destroying resources
    auto* appRenderer = core::Application::getInstance().getRenderer();
    if (appRenderer) appRenderer->unregisterPreview(this);

    if (charRenderer_) {
        charRenderer_->shutdown();
        charRenderer_.reset();
    }
    camera_.reset();
    if (scene_) { scene_->shutdown(); scene_.reset(); }
    modelLoaded_ = false;
    compositeRendered_ = false;
    instanceId_ = 0;
}

bool CharacterPreview::loadCharacter(game::Race race, game::Gender gender,
                                      uint8_t skin, uint8_t face,
                                      uint8_t hairStyle, uint8_t hairColor,
                                      uint8_t facialHair, bool useFemaleModel) {
    if (!charRenderer_ || !assetManager_ || !assetManager_->isInitialized()) {
        return false;
    }

    // Remove existing instance.
    // Must wait for GPU to finish - compositePass() may have recorded draw commands
    // referencing this instance's bone buffers earlier in the current frame.
    if (instanceId_ > 0) {
        if (vkCtx_) vkDeviceWaitIdle(vkCtx_->getDevice());
        charRenderer_->removeInstance(instanceId_);
        instanceId_ = 0;
        modelLoaded_ = false;
    }

    std::string m2Path = game::getPlayerModelPath(race, gender, useFemaleModel);

    auto m2Data = assetManager_->readFile(m2Path);
    if (m2Data.empty()) {
        LOG_WARNING("CharacterPreview: failed to read M2: ", m2Path);
        return false;
    }

    auto model = pipeline::M2Loader::load(m2Data);
    if (model.name.empty()) model.name = m2Path;

    // M2 version 264+ (WotLK) stores submesh/bone data in external .skin files.
    // Earlier versions (Classic ≤256, TBC ≤263) have skin data embedded in the M2.
    std::string skinPath = pipeline::skinPathForM2(m2Path);
    auto skinData = assetManager_->readFile(skinPath);
    if (!skinData.empty() && model.version >= 264) {
        pipeline::M2Loader::loadSkin(skinData, model);
    }

    if (!model.isValid()) {
        LOG_WARNING("CharacterPreview: invalid model: ", m2Path);
        return false;
    }

    if (camera_) {
        glm::vec3 frameMin = model.boundMin;
        glm::vec3 frameMax = model.boundMax;
        if (!model.vertices.empty()) {
            glm::vec3 tightMin(std::numeric_limits<float>::max());
            glm::vec3 tightMax(-std::numeric_limits<float>::max());
            for (const auto& v : model.vertices) {
                if (!isFiniteVec3(v.position)) continue;
                tightMin = glm::min(tightMin, v.position);
                tightMax = glm::max(tightMax, v.position);
            }
            if (tightMin.x <= tightMax.x && tightMin.y <= tightMax.y && tightMin.z <= tightMax.z) {
                frameMin = tightMin;
                frameMax = tightMax;
            }
        }
        frameCameraForModelBounds(*camera_, frameMin, frameMax);
        modelBoundMinZ_ = frameMin.z;
        modelBoundMaxZ_ = frameMax.z;
        fullBodyDistance_ = camera_->getPosition().y;
        // A player model is framed by its bounds; no creature portrait camera
        // is in play, and a stale one from the shared preview must not be.
        portraitCameraValid_ = false;
    }

    // Look up CharSections.dbc for all appearance textures
    uint32_t targetRaceId = static_cast<uint32_t>(race);
    uint32_t targetSexId = (gender == game::Gender::FEMALE ||
                            (gender == game::Gender::NONBINARY && useFemaleModel)) ? 1u : 0u;

    std::string faceLowerPath;
    std::string faceUpperPath;
    std::string hairScalpPath;
    std::vector<std::string> underwearPaths;
    bodySkinPath_.clear();
    baseLayers_.clear();

    auto charSectionsDbc = assetManager_->loadDBC("CharSections.dbc");
    if (charSectionsDbc) {
        const auto* csL = pipeline::getActiveDBCLayout()
            ? pipeline::getActiveDBCLayout()->getLayout("CharSections") : nullptr;
        const auto csF = pipeline::detectCharSectionsFields(charSectionsDbc.get(), csL);

        // The one reader, in pipeline/char_sections.hpp. This copy had no
        // fallback for a face the table does not carry - a character created
        // with a face number that has no row simply had no face - and it
        // matched the skin and underwear rows on variation 0, which the other
        // readers do not, so a table that numbers those rows any other way
        // found nothing here and everything there.
        pipeline::CharacterAppearance who;
        who.raceId = targetRaceId;
        who.sexId = targetSexId;
        who.skinId = static_cast<uint8_t>(skin);
        who.faceId = static_cast<uint8_t>(face);
        who.hairStyleId = static_cast<uint8_t>(hairStyle);
        who.hairColorId = static_cast<uint8_t>(hairColor);

        const auto sections = pipeline::resolveCharacterSections(
            charSectionsDbc.get(), csF, who,
            [](const std::string& path, void* ctx) {
                return static_cast<pipeline::AssetManager*>(ctx)->fileExists(path);
            },
            assetManager_);

        bodySkinPath_   = sections.bodySkin;
        skinExtraPath_  = sections.skinExtra;
        faceLowerPath   = sections.faceLower;
        faceUpperPath   = sections.faceUpper;
        hairScalpPath   = sections.hair;
        underwearPaths  = sections.underwear;

        LOG_INFO("CharSections lookup: skin=",
                 bodySkinPath_.empty() ? "(not found)" : bodySkinPath_,
                 " face=", sections.haveFace
                     ? (sections.exactFace ? faceLowerPath : faceLowerPath + " (nearest)")
                     : "(not found)",
                 " hair=", sections.haveHair ? hairScalpPath : "(not found)",
                 " underwear=", underwearPaths.size(), " textures");
    } else {
        LOG_WARNING("CharSections.dbc not loaded - no character textures");
    }

    // Assign texture filenames on model before GPU upload
    // pipeline/char_sections.hpp fills the runtime slots. This copy still
    // guarded types 1 and 6 with "only if the slot is empty", which is the trap
    // the skin-extra slot was already fixed for: a name in a runtime slot is not
    // a filename, and 'Ohren' is what a model in the wild puts there.
    {
        pipeline::CharacterSectionTextures resolved;
        resolved.bodySkin  = bodySkinPath_;
        resolved.skinExtra = skinExtraPath_;
        resolved.hair      = hairScalpPath;
        resolved.underwear = underwearPaths;
        // The race folder is the second component of the model path:
        // Character\Human\Female\HumanFemale.m2 -> Human. Taken from the path
        // rather than restated, so the two cannot disagree.
        std::string raceFolder;
        {
            const size_t first = m2Path.find('\\');
            const size_t second = (first == std::string::npos)
                ? std::string::npos : m2Path.find('\\', first + 1);
            if (first != std::string::npos && second != std::string::npos) {
                raceFolder = m2Path.substr(first + 1, second - first - 1);
            }
        }
        pipeline::applyCharacterTextures(model, resolved, raceFolder);
    }

    // Load external .anim files for sequences that store keyframes outside the M2.
    // Flag 0x20 = embedded data; when clear, animation lives in {ModelName}{SeqID}-{Var}.anim
    pipeline::loadExternalAnimations(*assetManager_, m2Path, m2Data, model);

    if (!charRenderer_->loadModel(model, PREVIEW_MODEL_ID)) {
        LOG_WARNING("CharacterPreview: failed to load model to GPU");
        return false;
    }
    // Composite body skin + face + underwear overlays
    if (!bodySkinPath_.empty()) {
        std::vector<std::string> layers;
        layers.push_back(bodySkinPath_);
        // Face lower texture composited onto body at the face region
        if (!faceLowerPath.empty()) {
            layers.push_back(faceLowerPath);
        }
        if (!faceUpperPath.empty()) {
            layers.push_back(faceUpperPath);
        }
        for (const auto& up : underwearPaths) {
            layers.push_back(up);
        }

        // Cache for later equipment compositing.
        // Keep baseLayers_ without the base skin (compositeWithRegions takes basePath separately).
        if (!faceLowerPath.empty()) baseLayers_.push_back(faceLowerPath);
        if (!faceUpperPath.empty()) baseLayers_.push_back(faceUpperPath);
        for (const auto& up : underwearPaths) baseLayers_.push_back(up);

        if (layers.size() > 1) {
            VkTexture* compositeTex = charRenderer_->compositeTextures(layers);
            if (compositeTex != nullptr) {
                for (size_t ti = 0; ti < model.textures.size(); ti++) {
                    if (model.textures[ti].type == 1) {
                        charRenderer_->setModelTexture(PREVIEW_MODEL_ID, static_cast<uint32_t>(ti), compositeTex);
                        break;
                    }
                }
            }
        } else {
            // Single layer (body skin only, no face/underwear overlays) - load directly
            VkTexture* skinTex = charRenderer_->loadTexture(bodySkinPath_);
            if (skinTex != nullptr) {
                for (size_t ti = 0; ti < model.textures.size(); ti++) {
                    if (model.textures[ti].type == 1) {
                        charRenderer_->setModelTexture(PREVIEW_MODEL_ID, static_cast<uint32_t>(ti), skinTex);
                        break;
                    }
                }
            }
        }
    }

    // If hair scalp texture was found, ensure it's loaded for type-6 slot
    if (!hairScalpPath.empty()) {
        VkTexture* hairTex = charRenderer_->loadTexture(hairScalpPath);
        if (hairTex != nullptr) {
            for (size_t ti = 0; ti < model.textures.size(); ti++) {
                if (model.textures[ti].type == 6) {
                    charRenderer_->setModelTexture(PREVIEW_MODEL_ID, static_cast<uint32_t>(ti), hairTex);
                    break;
                }
            }
        }
    }

    // Create instance at origin with current yaw
    instanceId_ = charRenderer_->createInstance(PREVIEW_MODEL_ID,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, modelYaw_),
        1.0f);

    if (instanceId_ == 0) {
        LOG_WARNING("CharacterPreview: failed to create instance");
        return false;
    }

    // Cache core appearance before geoset selection; the DBC resolver uses it.
    race_ = race;
    gender_ = gender;
    useFemaleModel_ = useFemaleModel;
    hairStyle_ = hairStyle;
    facialHair_ = facialHair;

    std::unordered_set<uint16_t> activeGeosets = buildBaseGeosets();
    charRenderer_->setActiveGeosets(instanceId_, activeGeosets);

    // Play idle animation (Stand = animation ID 0)
    charRenderer_->playAnimation(instanceId_, rendering::anim::STAND, true);

    // Cache the type-1 texture slot index so applyEquipment can update it.
    skinTextureSlotIndex_ = 0;
    for (size_t ti = 0; ti < model.textures.size(); ti++) {
        if (model.textures[ti].type == 1) {
            skinTextureSlotIndex_ = static_cast<uint32_t>(ti);
            break;
        }
    }

    modelLoaded_ = true;
    // Appearance changes recreate the character instance but keep the scene:
    // put the new instance back on its stand mark.
    applyPreviewView();
    LOG_INFO("CharacterPreview: loaded ", m2Path,
             " skin=", static_cast<int>(skin), " face=", static_cast<int>(face),
             " hair=", static_cast<int>(hairStyle), " hairColor=", static_cast<int>(hairColor),
             " facial=", static_cast<int>(facialHair));
    return true;
}

bool CharacterPreview::applyEquipment(const std::vector<game::EquipmentItem>& equipment) {
    if (!modelLoaded_ || instanceId_ == 0 || !charRenderer_ || !assetManager_ || !assetManager_->isInitialized()) {
        return false;
    }

    // Weapons first, and unconditionally: they depend on nothing below, while the
    // geoset/skin work that follows bails out early on characters whose body skin
    // could not be composited. Attaching last meant those characters showed no
    // weapon at all - and kept the previously selected character's weapon and
    // enchant, since detaching happens here too.
    attachWeapons(equipment);

    charRenderer_->clearTextureSlotOverride(instanceId_, static_cast<uint16_t>(skinTextureSlotIndex_));
    charRenderer_->setGroupTextureOverride(instanceId_, 15, nullptr);

    auto displayInfoDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
    if (!displayInfoDbc || !displayInfoDbc->isLoaded()) {
        LOG_WARNING("applyEquipment: ItemDisplayInfo.dbc not loaded");
        return false;
    }

    auto hasInvType = [&](std::initializer_list<uint8_t> types) -> bool {
        for (const auto& it : equipment) {
            if (it.displayModel == 0) continue;
            for (uint8_t t : types) {
                if (it.inventoryType == t) return true;
            }
        }
        return false;
    };

    auto findDisplayId = [&](std::initializer_list<uint8_t> types) -> uint32_t {
        for (const auto& it : equipment) {
            if (it.displayModel == 0) continue;
            for (uint8_t t : types) {
                if (it.inventoryType == t) return it.displayModel;
            }
        }
        return 0;
    };

    const auto* idiL = pipeline::getActiveDBCLayout()
        ? pipeline::getActiveDBCLayout()->getLayout("ItemDisplayInfo") : nullptr;
    const uint32_t geosetGroup1Field = idiL ? (*idiL)["GeosetGroup1"] : 7u;
    const uint32_t geosetGroup3Field = idiL ? (*idiL)["GeosetGroup3"] : 9u;

    auto getGeosetGroup = [&](uint32_t displayInfoId, uint32_t fieldIdx) -> uint32_t {
        if (displayInfoId == 0) return 0;
        int32_t recIdx = displayInfoDbc->findRecordById(displayInfoId);
        if (recIdx < 0) return 0;
        return displayInfoDbc->getUInt32(static_cast<uint32_t>(recIdx), fieldIdx);
    };

    // --- Geosets ---
    // M2 geoset IDs encode body part group × 100 + variant (e.g., 801 = group 8
    // (sleeves) variant 1, 1301 = group 13 (pants) variant 1). ItemDisplayInfo.dbc
    // provides the variant offset per equipped item; base IDs are per-group constants.
    std::unordered_set<uint16_t> geosets = buildBaseGeosets();

    auto eraseGroup = [&](uint16_t group) {
        for (auto it = geosets.begin(); it != geosets.end();) {
            if ((*it / 100) == group) {
                it = geosets.erase(it);
            } else {
                ++it;
            }
        }
    };

    // CharGeosets: group 4=gloves(forearm), 5=boots(shin), 8=sleeves, 13=pants
    std::unordered_set<uint16_t> modelGeosets;
    if (const auto* modelData = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
        for (const auto& batch : modelData->batches) {
            modelGeosets.insert(batch.submeshId);
        }
    }

    // core/geoset_rules.hpp, the same rule the world paths use.
    //
    // This copy took a second id as its fallback rather than looking inside the
    // group, so where the two ids given were the same - which every call below
    // does - it could only answer "the model has it" or "nothing", and a model
    // spelling that part with a different variant lost it entirely.
    auto pickGeoset = [&](uint16_t preferred, uint16_t fallback) -> uint16_t {
        const uint16_t chosen = core::resolveGeoset(preferred, modelGeosets);
        if (chosen != 0) return chosen;
        return (fallback != 0 && modelGeosets.count(fallback) > 0) ? fallback : 0;
    };

    auto lowestInGroup = [&](uint16_t group) -> uint16_t {
        return core::resolveGeoset(static_cast<uint16_t>(group * 100 + 2), modelGeosets);
    };

    uint16_t geosetGloves = pickGeoset(core::kGeosetBareForearms, core::kGeosetBareForearms);
    uint16_t geosetBoots = pickGeoset(core::kGeosetBareShins, lowestInGroup(5));
    uint16_t geosetSleeves = pickGeoset(core::kGeosetBareSleeves, core::kGeosetBareSleeves);
    uint16_t geosetPants = pickGeoset(core::kGeosetBarePants, core::kGeosetBarePants);

    // Chest/Shirt/Robe → group 8 (sleeves)
    {
        uint32_t did = findDisplayId({4, 5, 20});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetSleeves = pickGeoset(core::equippedGeoset(core::equipment::kChestBare, gg), core::kGeosetBareSleeves);
        // Robe kilt legs
        uint32_t gg3 = getGeosetGroup(did, geosetGroup3Field);
        if (gg3 > 0) geosetPants = pickGeoset(core::equippedGeoset(core::equipment::kRobeKiltBare, gg3), core::kGeosetBarePants);
    }
    // Legs → group 13 (trousers)
    {
        uint32_t did = findDisplayId({7});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetPants = pickGeoset(core::equippedGeoset(core::equipment::kLegsBare, gg), core::kGeosetBarePants);
    }
    // Boots → group 5 (shins)
    {
        uint32_t did = findDisplayId({8});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetBoots = pickGeoset(core::equippedGeoset(core::equipment::kBootsBare, gg), lowestInGroup(5));
    }
    // Gloves → group 4 (forearms)
    {
        uint32_t did = findDisplayId({10});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        if (gg > 0) geosetGloves = pickGeoset(core::equippedGeoset(core::equipment::kGlovesBare, gg), core::kGeosetBareForearms);
    }
    // Wrists/Bracers → group 8 (sleeves, only if chest/shirt didn't set it)
    {
        uint32_t did = findDisplayId({9});
        if (did != 0 && geosetSleeves == pickGeoset(core::kGeosetBareSleeves, core::kGeosetBareSleeves)) {
            uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
            if (gg > 0) geosetSleeves = pickGeoset(core::equippedGeoset(core::equipment::kChestBare, gg), core::kGeosetBareSleeves);
        }
    }
    // Belt → group 18 (buckle), falling back to the base variant so the waist
    // is not left empty - see the spawner's copy for what that costs.
    uint16_t geosetBelt = 0;
    {
        uint32_t did = findDisplayId({6});
        uint32_t gg = getGeosetGroup(did, geosetGroup1Field);
        geosetBelt = pickGeoset(
            gg > 0 ? core::equippedGeoset(core::equipment::kBeltBase, gg) : 0,
            core::equipment::kBeltBase);
    }

    eraseGroup(4);
    eraseGroup(5);
    eraseGroup(8);
    eraseGroup(13);
    eraseGroup(15);
    eraseGroup(18);
    if (geosetGloves != 0) geosets.insert(geosetGloves);
    if (geosetBoots != 0) geosets.insert(geosetBoots);
    if (geosetSleeves != 0) geosets.insert(geosetSleeves);
    if (geosetPants != 0) geosets.insert(geosetPants);
    if (geosetBelt != 0) geosets.insert(geosetBelt);
    uint16_t geosetCape = pickGeoset(
        hasInvType({16}) ? core::kGeosetWithCape : core::kGeosetNoCape,
        core::kGeosetNoCape);
    if (geosetCape != 0) geosets.insert(geosetCape); // Cloak mesh toggle (visual may still be limited)
    if (hasInvType({19})) {
        uint16_t geosetTabard = pickGeoset(core::kGeosetDefaultTabard, 0);
        if (geosetTabard != 0) geosets.insert(geosetTabard);
    }

    // Keep hair visible in the preview. The in-world renderer can hide hair
    // because it attaches helmet models, but this preview path does not yet
    // render head-slot attachments; hiding hair here leaves bald characters.

    charRenderer_->setActiveGeosets(instanceId_, geosets);

    // --- Textures (equipment overlays onto body skin) ---
    if (bodySkinPath_.empty()) return true; // geosets applied, but can't composite


    // Texture component region fields - use DBC layout when available, fall back to binary offsets.
    uint32_t texRegionFields[8];
    pipeline::getItemDisplayInfoTextureFields(*displayInfoDbc, idiL, texRegionFields);

    std::vector<std::pair<int, std::string>> regionLayers;
    regionLayers.reserve(32);

    for (const auto& it : equipment) {
        if (it.displayModel == 0) continue;
        int32_t recIdx = displayInfoDbc->findRecordById(it.displayModel);
        if (recIdx < 0) continue;

        for (int region = 0; region < 8; region++) {
            std::string texName = displayInfoDbc->getString(static_cast<uint32_t>(recIdx), texRegionFields[region]);
            if (texName.empty()) continue;

            const std::string fullPath = pipeline::resolveItemRegionTexture(
                *assetManager_, region, texName, gender_ == game::Gender::FEMALE);
            if (fullPath.empty()) continue;
            regionLayers.emplace_back(region, fullPath);
        }
    }

    if (!regionLayers.empty()) {
        VkTexture* newTex = charRenderer_->compositeWithRegions(bodySkinPath_, baseLayers_, regionLayers);
        if (newTex != nullptr) {
            charRenderer_->setTextureSlotOverride(instanceId_, static_cast<uint16_t>(skinTextureSlotIndex_), newTex);
        }
    }

    // Cloak texture (group 15) is separate from body compositing.
    if (hasInvType({16})) {
        uint32_t capeDisplayId = findDisplayId({16});
        if (capeDisplayId != 0) {
            int32_t capeRecIdx = displayInfoDbc->findRecordById(capeDisplayId);
            if (capeRecIdx >= 0) {
                std::vector<std::string> capeNames;
                auto addName = [&](const std::string& n) {
                    if (!n.empty() && std::find(capeNames.begin(), capeNames.end(), n) == capeNames.end()) {
                        capeNames.push_back(n);
                    }
                };
                std::string leftName = displayInfoDbc->getString(static_cast<uint32_t>(capeRecIdx), 3);
                std::string rightName = displayInfoDbc->getString(static_cast<uint32_t>(capeRecIdx), 4);
                if (gender_ == game::Gender::FEMALE) {
                    addName(rightName);
                    addName(leftName);
                } else {
                    addName(leftName);
                    addName(rightName);
                }

                // pipeline/item_textures.hpp knows where a cape's art is and
                // in what order to try for it.
                std::vector<std::string> candidates;
                for (const auto& nameRaw : capeNames) {
                    for (auto& c : pipeline::capeTextureCandidates(
                             nameRaw, gender_ == game::Gender::FEMALE)) {
                        if (std::find(candidates.begin(), candidates.end(), c) == candidates.end()) {
                            candidates.push_back(std::move(c));
                        }
                    }
                }
                VkTexture* whiteTex = charRenderer_->loadTexture("");
                for (const auto& c : candidates) {
                    VkTexture* capeTex = charRenderer_->loadTexture(c);
                    if (capeTex != nullptr && capeTex != whiteTex) {
                        charRenderer_->setGroupTextureOverride(instanceId_, 15, capeTex);
                        if (const auto* md = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
                            for (size_t ti = 0; ti < md->textures.size(); ti++) {
                                if (md->textures[ti].type == 2) {
                                    charRenderer_->setTextureSlotOverride(instanceId_, static_cast<uint16_t>(ti), capeTex);
                                }
                            }
                        }
                        break;
                    }
                }
            }
        }
    } else {
        if (const auto* md = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
            for (size_t ti = 0; ti < md->textures.size(); ti++) {
                if (md->textures[ti].type == 2) {
                    charRenderer_->clearTextureSlotOverride(instanceId_, static_cast<uint16_t>(ti));
                }
            }
        }
    }

    return true;
}

/// Any model, by path, with none of the appearance work.
///
/// loadCharacter builds a player: a race model, a composited skin, geosets
/// chosen from hair and facial hair, underwear. A creature is none of that -
/// its M2 names its own textures and has no geoset choices to make - so this
/// is the same three steps with the middle two thirds left out: read the M2,
/// frame the camera on its bounds, hand it to the renderer and stand it up.
///
/// The renderer is the one that already draws every unit in the world, so a
/// creature model needs nothing here that the world does not already do.
bool CharacterPreview::setBakedSkin(const std::string& bakePath) {
    if (!charRenderer_ || instanceId_ == 0 || bakePath.empty()) return false;
    VkTexture* tex = charRenderer_->loadTexture(bakePath);
    if (!tex) return false;
    charRenderer_->setTextureSlotOverride(
        instanceId_, static_cast<uint16_t>(skinTextureSlotIndex_), tex);
    return true;
}

bool CharacterPreview::loadCreature(
        const std::string& m2Path,
        const std::vector<std::pair<uint32_t, std::string>>& skins) {
    if (!charRenderer_ || !assetManager_ || !assetManager_->isInitialized()) {
        return false;
    }
    if (m2Path.empty()) return false;

    if (instanceId_ > 0) {
        charRenderer_->removeInstance(instanceId_);
        instanceId_ = 0;
    }

    pipeline::M2Model model;
    if (!loadPreviewM2(m2Path, model)) {
        LOG_WARNING("CharacterPreview: could not read creature model: ", m2Path);
        return false;
    }

    if (camera_) {
        // The declared bounds where there are any, and the vertices where
        // there are not - the same choice loadCharacter makes, and for the
        // same reason: a model whose header bounds are wrong frames wrong.
        glm::vec3 frameMin = model.boundMin;
        glm::vec3 frameMax = model.boundMax;
        if (!model.vertices.empty()) {
            glm::vec3 tightMin(std::numeric_limits<float>::max());
            glm::vec3 tightMax(-std::numeric_limits<float>::max());
            for (const auto& v : model.vertices) {
                if (!isFiniteVec3(v.position)) continue;
                tightMin = glm::min(tightMin, v.position);
                tightMax = glm::max(tightMax, v.position);
            }
            if (tightMin.x <= tightMax.x && tightMin.y <= tightMax.y &&
                tightMin.z <= tightMax.z) {
                frameMin = tightMin;
                frameMax = tightMax;
            }
        }
        frameCameraForModelBounds(*camera_, frameMin, frameMax);
        modelBoundMinZ_ = frameMin.z;
        modelBoundMaxZ_ = frameMax.z;
        fullBodyDistance_ = camera_->getPosition().y;
    }

    // The artist's own portrait framing, where the model carries one.
    portraitCameraValid_ = false;
    for (const auto& cam : model.cameras) {
        if (cam.type != 0) continue;
        if (!isFiniteVec3(cam.positionBase) || !isFiniteVec3(cam.targetBase)) break;
        portraitCameraPos_ = cam.positionBase;
        portraitCameraTarget_ = cam.targetBase;
        portraitCameraFovDegrees_ =
            (cam.fov > 0.0f && cam.fov < 3.14159f) ? glm::degrees(cam.fov) : 0.0f;
        portraitCameraValid_ = true;
        break;
    }

    if (!charRenderer_->loadModel(model, PREVIEW_MODEL_ID)) {
        LOG_WARNING("CharacterPreview: failed to upload creature model");
        return false;
    }

    instanceId_ = charRenderer_->createInstance(PREVIEW_MODEL_ID,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, modelYaw_),
        1.0f);
    if (instanceId_ == 0) {
        LOG_WARNING("CharacterPreview: failed to create creature instance");
        return false;
    }

    // The display's skins, into the slots the model declared for them. Without
    // this the geometry draws with no texture at all, which is what the target
    // frame's portrait showed: a white shape in the right silhouette.
    if (const auto* data = charRenderer_->getModelData(PREVIEW_MODEL_ID)) {
        for (size_t ti = 0; ti < data->textures.size(); ++ti) {
            for (const auto& [texType, path] : skins) {
                if (data->textures[ti].type != texType) continue;
                if (VkTexture* tex = charRenderer_->loadTexture(path)) {
                    charRenderer_->setModelTexture(PREVIEW_MODEL_ID,
                                                   static_cast<uint32_t>(ti), tex);
                }
                break;
            }
        }
    }

    // No geoset selection: a creature shows every submesh its skin declares,
    // which is what leaving the active set alone means.
    charRenderer_->playAnimation(instanceId_, rendering::anim::STAND, true);
    modelLoaded_ = true;
    return true;
}

bool CharacterPreview::loadPreviewM2(const std::string& m2Path, pipeline::M2Model& outModel) {
    if (!assetManager_) return false;
    return pipeline::loadM2WithSkin(*assetManager_, m2Path, outModel);  // m2_asset_loader.hpp
}

void CharacterPreview::attachWeapons(const std::vector<game::EquipmentItem>& equipment) {
    if (!charRenderer_ || !assetManager_ || instanceId_ == 0) return;

    // Attachment 1 = right hand, 2 = left hand.
    charRenderer_->detachWeapon(instanceId_, 1);
    charRenderer_->detachWeapon(instanceId_, 2);

    auto displayInfoDbc = assetManager_->loadDBC("ItemDisplayInfo.dbc");
    if (!displayInfoDbc || !displayInfoDbc->isLoaded()) return;


    struct WeaponSlot {
        std::initializer_list<uint8_t> invTypes;
        uint32_t attachmentId;
    };
    // Main hand also covers two-handers and ranged; off hand covers shields and held items.
    const WeaponSlot slots[] = {
        { .invTypes = {13, 17, 21, 15, 25, 26}, .attachmentId = 1 },
        { .invTypes = {14, 22, 23},             .attachmentId = 2 },
    };

    for (const auto& ws : slots) {
        uint32_t displayId = 0;
        uint32_t itemVisualId = 0;
        for (const auto& item : equipment) {
            if (item.displayModel == 0) continue;
            for (uint8_t t : ws.invTypes) {
                if (item.inventoryType == t) {
                    displayId = item.displayModel;
                    // SMSG_CHAR_ENUM already reports the enchant as its ItemVisual id.
                    itemVisualId = item.enchantment;
                    break;
                }
            }
            if (displayId != 0) break;
        }
        if (displayId == 0) continue;

        int32_t recIdx = displayInfoDbc->findRecordById(displayId);
        if (recIdx < 0) continue;

        const auto art = pipeline::readItemDisplayArt(*displayInfoDbc,
                                                      static_cast<uint32_t>(recIdx));
        if (art.modelFile.empty()) continue;
        const std::string& modelFile = art.modelFile;
        const std::string& textureName = art.textureName;

        pipeline::M2Model weaponModel;
        std::string m2Path = "Item\\ObjectComponents\\Weapon\\" + modelFile;
        if (!loadPreviewM2(m2Path, weaponModel)) {
            m2Path = "Item\\ObjectComponents\\Shield\\" + modelFile;
            if (!loadPreviewM2(m2Path, weaponModel)) {
                LOG_WARNING("CharacterPreview: failed to load weapon model ", modelFile);
                continue;
            }
        }

        std::string texturePath;
        if (!textureName.empty()) {
            texturePath = "Item\\ObjectComponents\\Weapon\\" + textureName + ".blp";
            if (!assetManager_->fileExists(texturePath)) {
                texturePath = "Item\\ObjectComponents\\Shield\\" + textureName + ".blp";
            }
        }

        const uint32_t weaponModelId = previewModelIdFor(m2Path);
        if (!charRenderer_->attachWeapon(instanceId_, ws.attachmentId, weaponModel,
                                         weaponModelId, texturePath)) {
            continue;
        }
        attachWeaponEnchantVisual(ws.attachmentId, itemVisualId);
    }
}

uint32_t CharacterPreview::previewModelIdFor(const std::string& assetKey) {
    auto it = previewModelIds_.find(assetKey);
    if (it != previewModelIds_.end()) return it->second;
    uint32_t id = nextPreviewModelId_++;
    previewModelIds_.emplace(assetKey, id);
    return id;
}

void CharacterPreview::attachWeaponEnchantVisual(uint32_t attachmentId, uint32_t itemVisualId) {
    if (itemVisualId == 0 || !charRenderer_ || !assetManager_) return;

    auto visualsDbc = assetManager_->loadDBC("ItemVisuals.dbc");
    auto effectsDbc = assetManager_->loadDBC("ItemVisualEffects.dbc");
    if (!visualsDbc || !visualsDbc->isLoaded() || !effectsDbc || !effectsDbc->isLoaded()) return;

    auto effectModels = pipeline::resolveItemVisualModels(itemVisualId, visualsDbc.get(),
                                                          effectsDbc.get());

    for (uint32_t visualSlot = 0; visualSlot < effectModels.size(); ++visualSlot) {
        const std::string& modelName = effectModels[visualSlot];
        if (modelName.empty()) continue;

        std::string m2Path = modelName;
        size_t dot = m2Path.rfind('.');
        m2Path = (dot != std::string::npos ? m2Path.substr(0, dot) : m2Path) + ".m2";

        pipeline::M2Model effectModel;
        if (!loadPreviewM2(m2Path, effectModel)) {
            LOG_WARNING("CharacterPreview: failed to load enchant visual ", m2Path);
            continue;
        }
        charRenderer_->attachWeaponEffect(instanceId_, attachmentId, visualSlot,
                                          effectModel, previewModelIdFor(m2Path));
    }
}

void CharacterPreview::setScene(const GlueSceneState& scene) {
    // Nothing to stand in front of when the background is meant to show
    // through.
    if (transparentBackground_ || !scene_) return;
    // show() reloads only when the model changes and re-applies the rest, so
    // a screen saying the same scene again costs a comparison.
    const bool placed = scene_->show(scene);
    if (!placed && !scene.model.empty()) {
        LOG_WARNING("CharacterPreview: scene ", scene.model, " could not be placed");
    }
    takeStageFromScene();
}

void CharacterPreview::clearScene() {
    if (scene_) scene_->clear();
    takeStageFromScene();
}

void CharacterPreview::takeStageFromScene() {
    previewStandPosition_ = glm::vec3(0.0f);
    previewViewDirection_ = glm::vec3(0.0f, 1.0f, 0.0f);
    modelYaw_ = 90.0f;
    if (scene_ && scene_->placed()) {
        // These scenes are authored in their own space - the human one sits
        // ~230 units from its origin - and carry the camera and the spot the
        // character stands on. Keep the character on the authored stand mark
        // and use the scene camera for the viewing direction; distance and
        // focus come from the portrait rig so scroll-wheel zoom can move
        // naturally toward the face.
        const GlueSceneStage stage = scene_->stage();
        if (stage.valid) {
            glm::vec3 toCamera = stage.cameraEye - stage.cameraTarget;
            if (glm::length(toCamera) < 0.001f) toCamera = glm::vec3(0.0f, 1.0f, 0.0f);
            previewStandPosition_ = stage.standPosition;
            previewViewDirection_ = glm::normalize(toCamera);
            modelYaw_ = glm::degrees(std::atan2(previewViewDirection_.y, previewViewDirection_.x));
            LOG_INFO("CharacterPreview: stand=(", stage.standPosition.x, ",",
                     stage.standPosition.y, ",", stage.standPosition.z, ")");
        }
    }
    applyPreviewView();
}

void CharacterPreview::update(float deltaTime) {
    if (charRenderer_ && modelLoaded_) {
        // Racial glue scenes place the character far from world origin (the
        // human scene is roughly 230 units away). Use this preview's camera for
        // animation distance checks; testing against the renderer's default
        // origin culled bone and weapon-attachment updates, leaving the paper
        // doll rigid and its equipped weapons invisible.
        const glm::vec3 cameraPos = camera_ ? camera_->getPosition()
                                            : previewStandPosition_;
        charRenderer_->update(deltaTime, cameraPos);
    }
    // The scene's own animation and particles, seen from the same camera.
    if (scene_ && camera_) scene_->update(deltaTime, *camera_);
}

void CharacterPreview::render() {
    // No-op - actual rendering happens in compositePass() called from Renderer::beginFrame()
}

void CharacterPreview::compositePass(VkCommandBuffer cmd, uint32_t frameIndex) {
    // Only composite when a UI screen actually requested it this frame
    if (!compositeRequested_) return;
    compositeRequested_ = false;

    if (!charRenderer_ || !camera_ || !modelLoaded_ || !scene_ || !scene_->isBuilt()) {
        return;
    }

    // The scene records the pass: its per-frame block carries this camera
    // and the lighting and fog of whatever is on show - the studio rig when
    // nothing is - and the character is drawn into it first, so the scene's
    // depth and its blended sheets see the figure.
    const uint32_t slot = frameIndex % GlueScene::kSlots;
    scene_->record(cmd, slot, *camera_,
                   [&](VkCommandBuffer c, VkDescriptorSet perFrameSet, const Camera& camera) {
        // Preview rendering bypasses Renderer::renderWorld(), so it must run
        // the same resource-preparation hook itself after server appearance
        // data has produced bone matrices.
        charRenderer_->prepareRender(slot);
        charRenderer_->render(c, perFrameSet, camera);
    });

    compositeRendered_ = true;
}

void CharacterPreview::rotate(float yawDelta) {
    modelYaw_ += yawDelta;
    if (instanceId_ > 0 && charRenderer_) {
        charRenderer_->setInstanceRotation(instanceId_, glm::vec3(0.0f, 0.0f, modelYaw_));
    }
}

void CharacterPreview::zoom(float wheelDelta) {
    if (!std::isfinite(wheelDelta) || wheelDelta == 0.0f) return;
    zoomLevel_ = std::clamp(zoomLevel_ + wheelDelta * 0.12f, 0.0f, 1.0f);
    applyPreviewView();
}

void CharacterPreview::setTransparentBackground(bool transparent) {
    transparentBackground_ = transparent;
    if (!scene_) return;
    // Nothing at all behind a portrait, so the frame art around it shows
    // through; the studio backdrop everywhere else.
    scene_->setClearColor(transparent ? glm::vec4(0.0f) : glm::vec4(0.05f, 0.05f, 0.1f, 1.0f));
    // The scene model behind the character is as opaque as the clear colour,
    // so it goes as well.
    if (transparent) clearScene();
}

void CharacterPreview::setPortraitFraming() {
    zoomLevel_ = 1.0f;
    // Straight at the character. The model is turned to face the camera in the
    // same breath, which is the part takeStageFromScene would otherwise be the
    // only place to do.
    previewViewDirection_ = glm::vec3(0.0f, 1.0f, 0.0f);
    modelYaw_ = glm::degrees(std::atan2(previewViewDirection_.y,
                                        previewViewDirection_.x));
    applyPreviewView();
}

void CharacterPreview::resetView() {
    zoomLevel_ = 0.0f;
    applyPreviewView();
}

void CharacterPreview::applyPreviewView() {
    if (!camera_) return;

    // A model that carries its own portrait camera is framed by it. The
    // fraction-of-height guess below assumes the bind pose is the drawn pose,
    // which holds for a player and not for a creature that hunches: a gnoll
    // stands 2.04 tall in bind pose, so 82% of it aimed at 1.67 while the
    // artist's own camera looks at 0.96, and the portrait showed the top of
    // its head. The camera is placed in model space, so it is offset by where
    // the model stands and the model is left in its own orientation.
    if (zoomLevel_ >= 1.0f && portraitCameraValid_) {
        // Room for the idle to move in.
        //
        // The artist's camera frames the pose Stand opens on, tightly. Left at
        // exactly that, a gnoll swings its head out of shot for most of the
        // loop. Backing off along the same axis keeps the framing the artist
        // chose - the angle, the target, the whole composition - and only
        // loosens the crop, which is the one thing an animated portrait needs
        // and a still one does not. One number, tune it by eye.
        constexpr float kPortraitHeadroom = 1.35f;
        const glm::vec3 focus = previewStandPosition_ + portraitCameraTarget_;
        const glm::vec3 camPos =
            focus + (portraitCameraPos_ - portraitCameraTarget_) * kPortraitHeadroom;
        camera_->setPosition(camPos);
        const glm::vec3 forward = glm::normalize(focus - camPos);
        camera_->setRotation(glm::degrees(std::atan2(forward.y, forward.x)),
                             glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f))));
        if (portraitCameraFovDegrees_ > 0.0f) {
            if (defaultFovDegrees_ <= 0.0f) defaultFovDegrees_ = camera_->getFovDegrees();
            camera_->setFov(portraitCameraFovDegrees_);
        }
        if (instanceId_ > 0 && charRenderer_) {
            charRenderer_->setInstancePosition(instanceId_, previewStandPosition_);
            charRenderer_->setInstanceRotation(instanceId_, glm::vec3(0.0f));
        }
        return;
    }

    // Whatever a portrait camera last set, handed back.
    if (defaultFovDegrees_ > 0.0f) camera_->setFov(defaultFovDegrees_);

    const float modelHeight = std::max(modelBoundMaxZ_ - modelBoundMinZ_, 0.1f);
    const float bodyFocusZ = (modelBoundMinZ_ + modelBoundMaxZ_) * 0.5f;
    const float faceFocusZ = modelBoundMinZ_ + modelHeight * 0.82f;
    const float focusZ = bodyFocusZ + (faceFocusZ - bodyFocusZ) * zoomLevel_;

    // Stay outside the near plane while getting close enough to inspect facial
    // textures and hair. Large races retain proportionally more distance.
    const float faceDistance = std::max(1.15f, modelHeight * 0.70f);
    const float distance = fullBodyDistance_ +
                           (std::min(faceDistance, fullBodyDistance_) - fullBodyDistance_) * zoomLevel_;
    const glm::vec3 focus = previewStandPosition_ + glm::vec3(0.0f, 0.0f, focusZ);
    const glm::vec3 camPos = focus + previewViewDirection_ * distance;
    camera_->setPosition(camPos);

    const glm::vec3 forward = glm::normalize(focus - camPos);
    const float yaw = glm::degrees(std::atan2(forward.y, forward.x));
    const float pitch = glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f)));
    camera_->setRotation(yaw, pitch);

    if (instanceId_ > 0 && charRenderer_) {
        charRenderer_->setInstancePosition(instanceId_, previewStandPosition_);
        charRenderer_->setInstanceRotation(instanceId_, glm::vec3(0.0f, 0.0f, modelYaw_));
    }
}

} // namespace rendering
} // namespace wowee
