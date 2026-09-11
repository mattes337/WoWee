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
#include "rendering/lighting_manager.hpp"
#include "rendering/renderer.hpp"
#include "rendering/terrain_manager.hpp"
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

    bool initialize(const std::string& dataPath) {
        if (!std::filesystem::exists(dataPath)) {
            error = "data path does not exist: " + dataPath;
            return false;
        }
        core::setEnvVar("WOW_DATA_PATH", dataPath.c_str());
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
        for (int i = 0; i < kWarmupFrames; ++i) {
            drawFrame(renderPos, yaw, pitch);
        }
        if (auto* terrain = renderer->getTerrainManager()) {
            int extra = 0;
            while (terrain->getRemainingTileCount() > 0 && extra < kMaxSettleFrames - kWarmupFrames) {
                drawFrame(renderPos, yaw, pitch);
                ++extra;
            }
        }
        // One more after the streamers go quiet, so the shot is of a frame
        // nothing changed during.
        drawFrame(renderPos, yaw, pitch);

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
            while (clock::now() < until) {
                const auto frameStart = clock::now();
                drawFrame(renderPos, yaw, pitch);
                const double ms = std::chrono::duration<double, std::milli>(
                    clock::now() - frameStart).count();
                worstMs = std::max(worstMs, ms);
                ++frames;
            }
            const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
            const double meanMs = frames ? (elapsed * 1000.0 / static_cast<double>(frames)) : 0.0;
            LOG_INFO("capture_scene: dwelled ", elapsed, "s over ", frames,
                     " frames - mean ", meanMs, "ms, worst ", worstMs, "ms");
            std::cout << "  dwell: " << frames << " frames in " << elapsed
                      << "s - mean " << meanMs << "ms, worst " << worstMs << "ms" << std::endl;
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

bool SceneCapture::initialize(const std::string& dataPath) {
    return impl_->initialize(dataPath);
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
