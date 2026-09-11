#pragma once

#include "game/character.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cstdint>
#include <memory>

namespace wowee {

namespace rendering {
    class VkContext;
    class Renderer;
    class Camera;
    class VkRenderTarget;
}

namespace pipeline {
    class AssetManager;
}

namespace tools {

/**
 * Character appearance and equipment configuration
 */
struct CaptureCharacter {
    game::Race race = game::Race::HUMAN;
    game::Gender gender = game::Gender::MALE;

    uint8_t skinColor = 0;
    uint8_t faceType = 0;
    uint8_t hairStyle = 0;
    uint8_t hairColor = 0;
    uint8_t facialHair = 0;

    std::array<uint32_t, 19> equipmentDisplayIds = {};
    std::array<uint8_t, 19> equipmentInventoryTypes = {};
    bool hasEquipment = false;
};

/**
 * Rendered entity information for JSON manifest
 */
struct CapturedEntity {
    std::string type;           // "wmo", "m2", "doodad", "terrain", "character"
    std::string modelPath;      // Path to the model file
    uint32_t instanceId = 0;    // Render instance ID
    glm::vec3 position{0.0f};   // World position
    glm::vec3 rotation{0.0f};   // Euler angles in degrees
    float scale = 1.0f;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    std::string name;
    uint32_t displayId = 0;
};

/**
 * Scene capture configuration
 */
struct CaptureConfig {
    // Output
    std::string outputPath;     // Output file prefix (e.g., "output/scene_001")
    uint32_t width = 1920;      // Render resolution width
    uint32_t height = 1080;     // Render resolution height

    // World/Map
    std::string mapName;        // Map name (e.g., "Azeroth", "Kalimdor") or WMO path for dungeons
    uint32_t mapId = 0;         // Map ID (0=Azeroth, 1=Kalimdor, etc.)
    bool isWmoMap = false;      // True for dungeon/instance maps (WMO-based)

    // Camera
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraTarget{0.0f};
    glm::vec3 cameraAngles{0.0f};    // pitch, yaw, roll (degrees)
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool useOrbitCamera = true; // Use position+target instead of angles

    // Character (optional)
    std::optional<CaptureCharacter> character;
    glm::vec3 characterPosition{0.0f};
    float characterOrientation = 0.0f;
    bool showCharacter = false;

    // Rendering options
    bool renderTerrain = true;
    bool renderWmo = true;
    bool renderM2 = true;
    bool renderDoodads = true;
    bool renderWater = true;
    bool renderSky = true;
    bool renderShadows = true;

    /// Keep drawing the settled scene for this many seconds before the shot.
    ///
    /// Zero is a capture: settle, draw one more, read back. A non-zero value is
    /// a soak - the same frame drawn over and over, which is what "five minutes
    /// in the world with validation on and no device loss" means when there is
    /// no server to stand in it. The frame time over the dwell is logged and
    /// printed, so it is also how this tool answers "did that get slower".
    float dwellSeconds = 0.0f;

    // Environment
    float timeOfDay = 12.0f;
    uint32_t weatherType = 0;
    float weatherIntensity = 0.0f;
};

/**
 * Scene capture result
 */
struct CaptureResult {
    bool success = false;
    std::string error;
    std::string screenshotPath;
    std::string manifestPath;
    std::vector<CapturedEntity> entities;
    uint32_t triangleCount = 0;
    uint32_t drawCallCount = 0;
};

/**
 * SceneCapture - Headless scene rendering and capture tool
 *
 * Renders a WoW scene to an off-screen buffer and saves:
 * 1. PNG screenshot of the rendered scene (NO UI - clean capture)
 * 2. JSON manifest listing all rendered entities
 *
 * Usage:
 *   SceneCapture capture;
 *   capture.initialize("/path/to/WoW/Data");
 *   CaptureConfig config;
 *   config.outputPath = "output/scene_001";
 *   config.mapName = "Azeroth";
 *   config.cameraPosition = {-9462, -67, 57};
 *   config.cameraTarget = {-9462, -67, 50};
 *   CaptureResult result = capture.capture(config);
 */
class SceneCapture {
public:
    SceneCapture();
    ~SceneCapture();

    SceneCapture(const SceneCapture&) = delete;
    SceneCapture& operator=(const SceneCapture&) = delete;

    /**
     * Initialize the capture system (headless Vulkan)
     * @param dataPath Path to WoW data directory (containing manifest.json)
     * @return True if successful
     */
    bool initialize(const std::string& dataPath);

    /**
     * Shutdown and cleanup resources
     */
    void shutdown();

    /**
     * Capture a scene based on configuration
     * @param config Capture configuration
     * @return Capture result with paths and entity list
     */
    CaptureResult capture(const CaptureConfig& config);

    /**
     * Get last error message
     */
    const std::string& getLastError() const;

    /**
     * A setting to push through SettingsPanel::setSettingValue before the
     * first frame is drawn.
     *
     * This is what makes a before and an after comparable: the same tool, the
     * same camera, the same time of day, one value moved. Repeatable - pass
     * --setting more than once and they are applied in order.
     */
    void addSettingOverride(const std::string& key, const std::string& value);

    /**
     * Draw the world as lines rather than surfaces, which is how a terrain
     * LOD change is looked at: the picture of the shaded ground is meant to be
     * the same one, and the wireframe is where the triangles went.
     */
    void setWireframe(bool on);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tools
} // namespace wowee
