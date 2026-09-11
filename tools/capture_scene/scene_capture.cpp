/**
 * scene_capture.cpp - the client, one frame of it, with nobody at the keyboard.
 *
 * The point of this tool is a before and an after. A rendering change that is
 * only visible in a picture cannot be reviewed by reading a diff, and it cannot
 * be reviewed by describing it either; two PNGs of the same camera with one
 * setting moved is the whole of the evidence. So this drives the real client -
 * the real asset manager, the real world loader, the real renderer and the real
 * shaders - rather than a second, simpler renderer that would answer questions
 * about itself.
 *
 * WHY IT IS NOT HEADLESS
 *
 * It opens a window. A Vulkan swapchain needs a surface, the renderer's whole
 * frame is built around one, and Renderer::captureScreenshot reads back the
 * swapchain image that was just presented. Rendering to an off-screen colour
 * target instead would mean a second path through every pass - which is a
 * second thing to be wrong, and the picture would then be of that path rather
 * than of the one players run. The window is small, short-lived and not
 * interacted with; on a machine with no display at all this tool cannot run,
 * and says so rather than pretending.
 *
 * WHERE THE ASSETS COME FROM
 *
 * Whatever the client reads, because this is the client. -d is WOW_DATA_PATH
 * and --install is WOW_INSTALL_PATH, and Application::initialize does the rest:
 * it calls pipeline::detectGameInstall on the install path, hands
 * AssetManager::setGameArchives the archives it found, and only then calls
 * AssetManager::initialize on the data path. So a data directory with a
 * manifest.json is read as an extracted tree, and one without is read out of
 * the installation's own MPQ archives - "No manifest in <data>; reading the
 * installation's archives directly" in the log is the second case, and it needs
 * no extraction step at all.
 *
 * WHY IT DOES NOT GO THROUGH Application::run()
 *
 * There is no server. Application's own loop starts at the login screen and
 * reaches the world through authentication, realm select and character select,
 * none of which exist here. What actually draws the world is
 * WorldLoader::loadOnlineWorldTerrain, which needs a renderer and an asset
 * manager and nothing else, and Renderer::renderWorld, which needs a camera.
 * This calls those two directly and leaves the state machine where it is.
 */

#include "tools/scene_capture.hpp"

#include "core/application.hpp"
#include "core/coordinates.hpp"
#include "core/env.hpp"
#include "core/logger.hpp"
#include "core/window.hpp"
#include "core/world_loader.hpp"
#include "rendering/camera.hpp"
#include "rendering/character_renderer.hpp"
#include "rendering/lighting_manager.hpp"
#include "rendering/renderer.hpp"
#include "rendering/terrain_manager.hpp"
#include "rendering/vk_context.hpp"
#include "ui/game_screen.hpp"
#include "ui/settings_panel.hpp"
#include "ui/ui_manager.hpp"

#include <SDL2/SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace wowee {
namespace tools {

namespace {

/// How many frames to draw before the shot. Terrain, buildings and doodads all
/// stream in over several frames and upload on the frame after they arrive, so
/// a picture taken on the first one is a picture of an empty valley.
constexpr int kWarmupFrames = 240;

/// And how long to wait for the streamers to go quiet before giving up on them.
constexpr int kMaxSettleFrames = 900;

/// The frame the shot is taken on, counted from the first one drawn after the
/// world loaded.
///
/// Fixed, and that is the whole point. Every animated thing in this client
/// advances by the delta this tool hands Renderer::update, which is a constant
/// 1/60 - so the phase of the drifting clouds, the water, the wind in the
/// foliage and every particle is a function of how many frames have been drawn
/// and nothing else. The settle loop below stops when the terrain streamer goes
/// quiet, which is a different count on every run, and a pair of renders that
/// stopped at different counts is a pair with different clouds in it. That is
/// not a subtle effect: a camera with sky in it read 94 % of pixels changed and
/// an SSIM of 0.88 between two renders whose only intended difference was one
/// setting, because the sky had moved between them.
///
/// So the settle loop is followed by however many more frames it takes to reach
/// this number, and the shot is always frame kShotFrame. Two renders of one
/// camera are then the same frame with one thing moved, which is the only thing
/// this tool is for.
constexpr int kShotFrame = 900;

}  // namespace

class SceneCapture::Impl {
public:
    std::unique_ptr<core::Application> app;
    std::string error;
    /// key=value pairs to push through SettingsPanel::setSettingValue before
    /// the first frame. This is what makes a before and an after the same
    /// picture with one thing moved.
    std::vector<std::pair<std::string, std::string>> settings;
    bool wireframe = false;
    /// Whether the client's own character model is left in the frame.
    /// Off unless --char asked for one; see drawFrame.
    bool showCharacterModel = false;

    bool initialize(const std::string& dataPath, const std::string& installPath) {
        if (!std::filesystem::exists(dataPath)) {
            error = "data path does not exist: " + dataPath;
            return false;
        }
        core::setEnvVar("WOW_DATA_PATH", dataPath.c_str());
        // The installation whose archives are read when the data path holds no
        // extracted tree. Application::initialize already calls
        // detectGameInstall on WOW_INSTALL_PATH and hands the archives to the
        // asset manager before it initializes; --install is that environment
        // variable named on the command line, so a capture can be a single
        // line rather than a line and a shell export.
        if (!installPath.empty()) {
            core::setEnvVar("WOW_INSTALL_PATH", installPath.c_str());
        }
        // Nobody is here to click a box. Without this, a start-up failure -
        // archives that will not open is the common one - stops the tool on a
        // modal dialog for as long as the run is given, which reads as a hang
        // rather than as the error it printed.
        core::setEnvVar("WOWEE_NO_ERROR_DIALOG", "1");
        // Doodad animation phases are drawn from a random-seeded generator, so
        // two runs of one camera are two different forests. Pinned here rather
        // than in the client: a player wants the trees out of step, and a
        // before and an after have to be the same frame.
        core::setEnvVar("WOWEE_M2_ANIM_SEED", "20260911");
        // Nothing here is going to log in, and a client that spends its first
        // seconds resolving a realmlist is a client that takes seconds longer
        // to answer.
        core::setEnvVar("WOWEE_NO_GLUE_BACKDROP", "1");

        app = std::make_unique<core::Application>();
        if (!app->initialize()) {
            error = "the client could not initialise - see logs/wowee.log";
            app.reset();
            return false;
        }
        return true;
    }

    void applySettings() {
        if (!app || !app->getUIManager()) return;
        auto& panel = app->getUIManager()->getGameScreen().getSettingsPanel();
        for (const auto& [key, value] : settings) {
            if (!panel.setSettingValue(key, value)) {
                LOG_WARNING("capture_scene: setting '", key, "' was refused");
            } else {
                LOG_INFO("capture_scene: ", key, " = ", value);
            }
        }
    }

    /// One frame, from the outside. Renderer::update drives the camera
    /// controller and the streamers; the camera is re-asserted around it
    /// because a controller told to follow a character it does not have will
    /// otherwise put the view back where it thinks the player is.
    void drawFrame(const glm::vec3& renderPos, float yaw, float pitch) {
        auto* renderer = app->getRenderer();
        auto* camera = renderer->getCamera();
        camera->setPosition(renderPos);
        camera->setRotation(yaw, pitch);
        renderer->update(1.0f / 60.0f);
        camera->setPosition(renderPos);
        camera->setRotation(yaw, pitch);
        // An ImGui frame with nothing in it.
        //
        // Renderer::endFrame replays ImGui::GetDrawData() unconditionally, and
        // GetDrawData answers the last draw data that was built - so a frame
        // that never opens an ImGui frame re-submits the previous one. The
        // previous one here is the world loader's last loading-screen frame,
        // which draws a full-screen image out of a LoadingScreen that lives on
        // that function's stack and is destroyed the moment it returns. Every
        // frame after the load then sampled a destroyed image view, which is
        // what took the device down a second or two into the world. Opening and
        // closing an empty frame is also exactly what this tool wants: no UI in
        // the picture.
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::Render();
        // The sun's shadow is centred on the player, and there is no player
        // here. Left at the origin, the cascade fit reads that as "the world
        // has not been placed yet" and answers a zero matrix, and the shadow
        // pass then draws nothing at all - which would make every shadow
        // comparison a pair of identical pictures.
        renderer->getCharacterPosition() = renderPos;
        // And the model that position also moves is put out of the picture.
        //
        // Renderer::update syncs the character instance to characterPosition
        // every frame, so the line above did not only aim the cascades - it
        // parked the player model on the lens. Every shot this tool had taken
        // carried a wall of robe or a forearm across a corner of the frame, at
        // whatever part of the model the near plane happened to cut, and the
        // camera positions were being blamed for it. Hidden rather than
        // removed: the instance is the client's own and the follow target it
        // feeds is what keeps the shadow fit honest.
        if (!showCharacterModel) {
            if (auto* chars = renderer->getCharacterRenderer()) {
                const uint32_t id = renderer->getCharacterInstanceId();
                if (id > 0) chars->setInstanceVisible(id, false);
            }
        }
        renderer->beginFrame();
        renderer->renderWorld(app->getWorld(), nullptr);
        renderer->endFrame();
        // Nobody is at the keyboard, but a window that never drains its event
        // queue is a window the compositor calls unresponsive, and on Windows
        // it stops being composited at all - which is a black swapchain image
        // to read back.
        SDL_PumpEvents();
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    }

    CaptureResult capture(const CaptureConfig& config) {
        CaptureResult result;
        if (!app) {
            result.error = "not initialised";
            return result;
        }
        auto* renderer = app->getRenderer();
        if (!renderer || !renderer->getCamera()) {
            result.error = "no renderer";
            return result;
        }

        showCharacterModel = config.showCharacter;
        applySettings();
        renderer->setWireframeMode(wireframe);

        if (auto* lighting = renderer->getLightingManager()) {
            lighting->setTimeOfDay(std::clamp(config.timeOfDay, 0.0f, 24.0f) / 24.0f);
        }

        // Where the camera is, in the three coordinate systems this codebase
        // keeps apart. The arguments are server coordinates - the same numbers
        // .gps prints - because that is what anyone reading a bug report has.
        const glm::vec3 canonical = core::coords::serverToCanonical(config.cameraPosition);
        const glm::vec3 renderPos = core::coords::canonicalToRender(canonical);
        const glm::vec3 targetCanonical = core::coords::serverToCanonical(config.cameraTarget);
        const glm::vec3 targetRender = core::coords::canonicalToRender(targetCanonical);

        float yaw = config.cameraAngles.y;
        float pitch = config.cameraAngles.x;
        if (config.useOrbitCamera) {
            const glm::vec3 dir = targetRender - renderPos;
            const float horizontal = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (horizontal > 1e-4f || std::abs(dir.z) > 1e-4f) {
                yaw = glm::degrees(std::atan2(dir.y, dir.x));
                pitch = glm::degrees(std::atan2(dir.z, std::max(horizontal, 1e-4f)));
            }
        }

        auto* camera = renderer->getCamera();
        camera->setFov(config.fov);
        camera->setPosition(renderPos);
        camera->setRotation(yaw, pitch);

        // The world. loadOnlineWorldTerrain drives its own loading screen and
        // presents its own frames, which is why it can be called from here at
        // all: it does not need a main loop around it.
        auto* loader = app->getWorldLoader();
        if (!loader) {
            result.error = "no world loader";
            return result;
        }
        const uint32_t mapId = config.mapId;
        loader->loadOnlineWorldTerrain(mapId, config.cameraPosition.x, config.cameraPosition.y,
                                       config.cameraPosition.z);

        // Let it settle. The terrain manager streams tiles in on worker threads
        // and uploads them on the frame after they land, so the number of
        // frames matters more than the wall clock.
        int framesDrawn = 0;
        for (int i = 0; i < kWarmupFrames; ++i) {
            drawFrame(renderPos, yaw, pitch);
            ++framesDrawn;
        }
        if (auto* terrain = renderer->getTerrainManager()) {
            while (terrain->getRemainingTileCount() > 0 && framesDrawn < kMaxSettleFrames) {
                drawFrame(renderPos, yaw, pitch);
                ++framesDrawn;
            }
        }
        const int settledAt = framesDrawn;
        // And on to the fixed frame the shot is taken on, so that the animated
        // half of the scene is at the same phase in both renders of a pair.
        while (framesDrawn < kShotFrame) {
            drawFrame(renderPos, yaw, pitch);
            ++framesDrawn;
        }
        LOG_INFO("capture_scene: streamers went quiet at frame ", settledAt,
                 "; the shot is frame ", framesDrawn);

        // The soak, if one was asked for. Frames are timed individually rather
        // than averaged over the wall clock so that a single long frame - a
        // pipeline rebuild, a tile finalising - is visible as a maximum rather
        // than hidden in a mean.
        if (config.dwellSeconds > 0.0f) {
            using clock = std::chrono::steady_clock;
            const auto start = clock::now();
            const auto until = start + std::chrono::duration_cast<clock::duration>(
                std::chrono::duration<double>(config.dwellSeconds));
            uint64_t frames = 0;
            double worstMs = 0.0;
            // Where the GPU spent the frame, as the renderer's own timestamps
            // report it. A technique that costs a fraction of a millisecond
            // cannot be measured by differencing two whole-frame means - at
            // this camera those vary by more than a millisecond between runs -
            // but the pass's own timestamp reads it directly.
            std::vector<std::pair<std::string, std::pair<double, uint64_t>>> gpuSums;
            auto* vkCtx = renderer->getVkContext();
            while (clock::now() < until) {
                const auto frameStart = clock::now();
                drawFrame(renderPos, yaw, pitch);
                const double ms = std::chrono::duration<double, std::milli>(
                    clock::now() - frameStart).count();
                worstMs = std::max(worstMs, ms);
                ++frames;
                if (vkCtx) {
                    for (const auto& [label, marked] : vkCtx->gpuTimings()) {
                        if (!label) continue;
                        auto it = std::find_if(gpuSums.begin(), gpuSums.end(),
                                               [&](const auto& e) { return e.first == label; });
                        if (it == gpuSums.end()) {
                            gpuSums.emplace_back(label, std::make_pair(marked, uint64_t{1}));
                        } else {
                            it->second.first += marked;
                            it->second.second += 1;
                        }
                    }
                }
            }
            const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
            const double meanMs = frames ? (elapsed * 1000.0 / static_cast<double>(frames)) : 0.0;
            LOG_INFO("capture_scene: dwelled ", elapsed, "s over ", frames,
                     " frames - mean ", meanMs, "ms, worst ", worstMs, "ms");
            std::cout << "  dwell: " << frames << " frames in " << elapsed
                      << "s - mean " << meanMs << "ms, worst " << worstMs << "ms" << std::endl;
            for (const auto& [label, acc] : gpuSums) {
                if (acc.second == 0) continue;
                const double mean = acc.first / static_cast<double>(acc.second);
                LOG_INFO("capture_scene: GPU ", label, " ", mean, "ms over ", acc.second,
                         " frames");
                std::cout << "  gpu " << label << ": " << mean << "ms over " << acc.second
                          << " frames" << std::endl;
            }
        }

        std::error_code ec;
        const std::filesystem::path out(config.outputPath + ".png");
        if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
        if (!renderer->captureScreenshot(out.string())) {
            result.error = "the swapchain image could not be read back";
            return result;
        }

        result.success = true;
        result.screenshotPath = out.string();
        result.manifestPath.clear();
        return result;
    }

    void shutdown() {
        if (app) {
            app->shutdown();
            app.reset();
        }
    }
};

SceneCapture::SceneCapture() : impl_(std::make_unique<Impl>()) {}
SceneCapture::~SceneCapture() = default;

bool SceneCapture::initialize(const std::string& dataPath, const std::string& installPath) {
    return impl_->initialize(dataPath, installPath);
}

void SceneCapture::shutdown() { impl_->shutdown(); }

CaptureResult SceneCapture::capture(const CaptureConfig& config) {
    return impl_->capture(config);
}

const std::string& SceneCapture::getLastError() const { return impl_->error; }

void SceneCapture::addSettingOverride(const std::string& key, const std::string& value) {
    impl_->settings.emplace_back(key, value);
}

void SceneCapture::setWireframe(bool on) { impl_->wireframe = on; }

}  // namespace tools
}  // namespace wowee
