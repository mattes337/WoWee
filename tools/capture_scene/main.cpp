/**
 * capture_scene - Headless WoW scene capture tool
 *
 * Renders a WoW scene to PNG screenshot + JSON entity manifest.
 * All UI is hidden during capture for clean screenshots.
 *
 * Usage:
 *   capture_scene [options] -o <output_prefix> -d <data_path>
 *
 * Examples:
 *   # Capture scene at specific location
 *   capture_scene -d /data -m Azeroth -c -9462,-67,57 -t -9462,-67,50 -o scene_001
 *
 *   # Capture with character
 *   capture_scene -d /data -m Azeroth -c -9462,-67,57 --char --char-pos -9462,-67,57 \
 *                 --race human --gender male --equip helm=1234,chest=5678 -o scene_002
 *
 *   # Capture dungeon (WMO-based map)
 *   capture_scene -d /data --wmo "World/wmo/Dungeon/L_Deadmines/L_Deadmines.wmo" \
 *                 -c 0,0,0 -o dungeon_001
 *
 * Output:
 *   <output_prefix>.png  - Screenshot of the rendered scene
 *   <output_prefix>.json - Entity manifest (WMO, M2, doodads, characters)
 */

#include "tools/scene_capture.hpp"
#include "core/logger.hpp"

#include <glm/glm.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include <utility>

using namespace wowee;
using namespace wowee::tools;

void printUsage(const char* progName) {
    std::cout << "WoW Scene Capture Tool\n";
    std::cout << "Renders a scene to PNG screenshot + JSON entity manifest (UI hidden)\n\n";
    std::cout << "Usage: " << progName << " -d <data_path> [options] -o <output_prefix>\n\n";

    std::cout << "Required:\n";
    std::cout << "  -d, --data <path>        Wowee data directory. An extracted tree with a\n";
    std::cout << "                           manifest.json, or - with --install - a directory\n";
    std::cout << "                           that holds only what wowee generates\n";
    std::cout << "  -o, --output <path>      Output file prefix (e.g., output/scene_001)\n\n";

    std::cout << "Reading an installation directly (no extraction):\n";
    std::cout << "  --install <path>         A game folder, or its Data directory. Its MPQ\n";
    std::cout << "                           archives answer every asset read, exactly as they\n";
    std::cout << "                           do for the client. Same as WOW_INSTALL_PATH\n\n";

    std::cout << "Map Options:\n";
    std::cout << "  -m, --map <name>         World map name (Azeroth, Kalimdor, Northrend, Outland)\n";
    std::cout << "  --map-id <id>            Map ID (0=Azeroth, 1=Kalimdor, 530=Outland, 571=Northrend)\n";
    std::cout << "  --wmo <path>             WMO path for dungeon/instance maps\n\n";

    std::cout << "Camera Options:\n";
    std::cout << "  -c, --camera <x,y,z>     Camera world position (server coordinates)\n";
    std::cout << "  -t, --target <x,y,z>     Camera look-at target (for orbit camera)\n";
    std::cout << "  --angles <p,y,r>         Camera angles: pitch, yaw, roll in degrees\n";
    std::cout << "  --fov <degrees>          Field of view (default: 60)\n";
    std::cout << "  --near <dist>            Near clipping plane (default: 0.1)\n";
    std::cout << "  --far <dist>             Far clipping plane (default: 1000)\n\n";

    std::cout << "Character Options:\n";
    std::cout << "  --char                   Enable character rendering\n";
    std::cout << "  --char-pos <x,y,z>       Character world position\n";
    std::cout << "  --char-yaw <degrees>     Character facing direction (default: 0)\n";
    std::cout << "  --race <name>            Character race (human, orc, dwarf, nightelf, etc.)\n";
    std::cout << "  --gender <m|f>           Character gender (male/female)\n";
    std::cout << "  --skin <0-255>           Skin color variation\n";
    std::cout << "  --face <0-255>           Face type\n";
    std::cout << "  --hair <0-255>           Hair style\n";
    std::cout << "  --hair-color <0-255>     Hair color\n";
    std::cout << "  --facial-hair <0-255>    Facial hair style\n";
    std::cout << "  --equip <slot=id,...>    Equipment display IDs (from ItemDisplayInfo.dbc)\n\n";

    std::cout << "Rendering Options:\n";
    std::cout << "  --width <pixels>         Render width (default: 1920)\n";
    std::cout << "  --height <pixels>        Render height (default: 1080)\n";
    std::cout << "  --time <0-24>            Time of day for lighting (default: 12)\n";
    std::cout << "  --dwell <seconds>        Keep drawing the settled scene this long\n";
    std::cout << "                           before the shot, and report frame time\n";
    std::cout << "  --weather <type>         Weather type (0=none, 1=rain, 3=snow)\n";
    std::cout << "  --weather-intensity <0-1> Weather intensity\n";
    std::cout << "  --no-terrain             Disable terrain rendering\n";
    std::cout << "  --no-wmo                 Disable WMO rendering\n";
    std::cout << "  --no-m2                  Disable M2/doodad rendering\n";
    std::cout << "  --no-water               Disable water rendering\n";
    std::cout << "  --no-sky                 Disable sky/atmosphere rendering\n";
    std::cout << "  --no-shadows             Disable shadow rendering\n\n";

    std::cout << "Other:\n";
    std::cout << "  -h, --help               Show this help message\n";
    std::cout << "  -v, --verbose            Enable verbose logging\n";
    std::cout << "  -q, --quiet              Suppress non-error output\n\n";

    std::cout << "Equipment Slots (for --equip):\n";
    std::cout << "  helm, shoulders, shirt, chest, belt, legs, feet,\n";
    std::cout << "  wrists, hands, tabard, cape, mainhand, offhand, ranged, relic\n";
    std::cout << "  Example: --equip helm=1234,chest=5678,legs=9012\n\n";

    std::cout << "Coordinates:\n";
    std::cout << "  Uses WoW server coordinates (same as .gps output).\n";
    std::cout << "  Y is vertical (height), X/Z are horizontal.\n";
}

// Parse "x,y,z" string to glm::vec3
bool parseVec3(const std::string& str, glm::vec3& out) {
    float x, y, z;
    if (sscanf(str.c_str(), "%f,%f,%f", &x, &y, &z) != 3) {
        return false;
    }
    out = glm::vec3(x, y, z);
    return true;
}

// Parse race name to enum
game::Race parseRace(const std::string& name) {
    static const std::unordered_map<std::string, game::Race> races = {
        {"human", game::Race::HUMAN},
        {"orc", game::Race::ORC},
        {"dwarf", game::Race::DWARF},
        {"nightelf", game::Race::NIGHT_ELF},
        {"night-elf", game::Race::NIGHT_ELF},
        {"undead", game::Race::UNDEAD},
        {"scourge", game::Race::UNDEAD},
        {"tauren", game::Race::TAUREN},
        {"gnome", game::Race::GNOME},
        {"troll", game::Race::TROLL},
        {"bloodelf", game::Race::BLOOD_ELF},
        {"blood-elf", game::Race::BLOOD_ELF},
        {"draenei", game::Race::DRAENEI}
    };

    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    auto it = races.find(lower);
    return it != races.end() ? it->second : game::Race::HUMAN;
}

// Parse equipment slot string
bool parseEquipment(const std::string& str, CaptureCharacter& charConfig) {
    static const std::unordered_map<std::string, int> slots = {
        {"helm", 0}, {"head", 0},
        {"shoulders", 1}, {"shoulder", 1},
        {"shirt", 2},
        {"chest", 3},
        {"belt", 4}, {"waist", 4},
        {"legs", 5}, {"leggings", 5},
        {"feet", 6}, {"boots", 6},
        {"wrists", 7}, {"wrist", 7},
        {"hands", 8}, {"gloves", 8},
        {"tabard", 9},
        {"cape", 10}, {"cloak", 10},
        {"mainhand", 11}, {"main", 11},
        {"offhand", 12}, {"off", 12},
        {"ranged", 13},
        {"relic", 14}
    };

    std::istringstream ss(str);
    std::string token;

    while (std::getline(ss, token, ',')) {
        size_t eqPos = token.find('=');
        if (eqPos == std::string::npos) continue;

        std::string slotName = token.substr(0, eqPos);
        std::string displayIdStr = token.substr(eqPos + 1);

        // Trim whitespace
        slotName.erase(0, slotName.find_first_not_of(" \t"));
        slotName.erase(slotName.find_last_not_of(" \t") + 1);

        std::transform(slotName.begin(), slotName.end(), slotName.begin(),
                       [](unsigned char c){ return std::tolower(c); });

        auto it = slots.find(slotName);
        if (it != slots.end()) {
            try {
                uint32_t displayId = std::stoul(displayIdStr);
                charConfig.equipmentDisplayIds[it->second] = displayId;
                charConfig.hasEquipment = true;
            } catch (...) {
                std::cerr << "Warning: Invalid display ID: " << displayIdStr << "\n";
            }
        } else {
            std::cerr << "Warning: Unknown equipment slot: " << slotName << "\n";
        }
    }

    return true;
}

int main(int argc, char* argv[]) {
    // Default configuration
    CaptureConfig config;
    std::string dataPath;
    std::string installPath;
    bool verbose = false;
    bool quiet = false;
    bool showCharacter = false;
    bool wireframe = false;
    std::vector<std::pair<std::string, std::string>> settingOverrides;
    CaptureCharacter charConfig;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        }
        else if (arg == "-q" || arg == "--quiet") {
            quiet = true;
        }
        else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            config.outputPath = argv[++i];
        }
        else if ((arg == "-d" || arg == "--data") && i + 1 < argc) {
            dataPath = argv[++i];
        }
        else if (arg == "--install" && i + 1 < argc) {
            installPath = argv[++i];
        }
        else if ((arg == "-m" || arg == "--map") && i + 1 < argc) {
            config.mapName = argv[++i];
        }
        else if (arg == "--map-id" && i + 1 < argc) {
            try {
                config.mapId = std::stoul(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid map ID\n";
                return 1;
            }
        }
        else if (arg == "--wmo" && i + 1 < argc) {
            config.mapName = argv[++i];
            config.isWmoMap = true;
        }
        else if ((arg == "-c" || arg == "--camera") && i + 1 < argc) {
            if (!parseVec3(argv[++i], config.cameraPosition)) {
                std::cerr << "Error: Invalid camera position format (use x,y,z)\n";
                return 1;
            }
        }
        else if ((arg == "-t" || arg == "--target") && i + 1 < argc) {
            if (!parseVec3(argv[++i], config.cameraTarget)) {
                std::cerr << "Error: Invalid camera target format (use x,y,z)\n";
                return 1;
            }
            config.useOrbitCamera = true;
        }
        else if (arg == "--angles" && i + 1 < argc) {
            if (!parseVec3(argv[++i], config.cameraAngles)) {
                std::cerr << "Error: Invalid camera angles format (use pitch,yaw,roll)\n";
                return 1;
            }
            config.useOrbitCamera = false;
        }
        else if (arg == "--fov" && i + 1 < argc) {
            try {
                config.fov = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid FOV value\n";
                return 1;
            }
        }
        else if (arg == "--near" && i + 1 < argc) {
            try {
                config.nearPlane = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid near plane value\n";
                return 1;
            }
        }
        else if (arg == "--far" && i + 1 < argc) {
            try {
                config.farPlane = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid far plane value\n";
                return 1;
            }
        }
        else if (arg == "--width" && i + 1 < argc) {
            try {
                config.width = std::stoul(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid width value\n";
                return 1;
            }
        }
        else if (arg == "--height" && i + 1 < argc) {
            try {
                config.height = std::stoul(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid height value\n";
                return 1;
            }
        }
        else if (arg == "--time" && i + 1 < argc) {
            try {
                config.timeOfDay = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid time value\n";
                return 1;
            }
        }
        else if (arg == "--dwell" && i + 1 < argc) {
            try {
                config.dwellSeconds = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid dwell value\n";
                return 1;
            }
        }
        else if (arg == "--weather" && i + 1 < argc) {
            try {
                config.weatherType = std::stoul(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid weather type\n";
                return 1;
            }
        }
        else if (arg == "--weather-intensity" && i + 1 < argc) {
            try {
                config.weatherIntensity = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid weather intensity\n";
                return 1;
            }
        }
        else if (arg == "--no-terrain") {
            config.renderTerrain = false;
        }
        else if (arg == "--no-wmo") {
            config.renderWmo = false;
        }
        else if (arg == "--no-m2") {
            config.renderM2 = false;
            config.renderDoodads = false;
        }
        else if (arg == "--no-water") {
            config.renderWater = false;
        }
        else if (arg == "--no-sky") {
            config.renderSky = false;
        }
        else if (arg == "--no-shadows") {
            config.renderShadows = false;
        }
        else if (arg == "--wireframe") {
            wireframe = true;
        }
        else if (arg == "--setting" && i + 1 < argc) {
            std::string pair = argv[++i];
            const size_t eq = pair.find('=');
            if (eq == std::string::npos) {
                std::cerr << "Error: --setting wants key=value, not: " << pair << "\n";
                return 1;
            }
            settingOverrides.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
        }
        else if (arg == "--char") {
            showCharacter = true;
            config.showCharacter = true;
        }
        else if (arg == "--char-pos" && i + 1 < argc) {
            if (!parseVec3(argv[++i], config.characterPosition)) {
                std::cerr << "Error: Invalid character position format (use x,y,z)\n";
                return 1;
            }
        }
        else if (arg == "--char-yaw" && i + 1 < argc) {
            try {
                config.characterOrientation = std::stof(argv[++i]);
            } catch (...) {
                std::cerr << "Error: Invalid character yaw value\n";
                return 1;
            }
        }
        else if (arg == "--race" && i + 1 < argc) {
            charConfig.race = parseRace(argv[++i]);
            showCharacter = true;
            config.showCharacter = true;
        }
        else if (arg == "--gender" && i + 1 < argc) {
            std::string g = argv[++i];
            std::transform(g.begin(), g.end(), g.begin(),
                           [](unsigned char c){ return std::tolower(c); });
            charConfig.gender = (g == "f" || g == "female") ? game::Gender::FEMALE : game::Gender::MALE;
        }
        else if (arg == "--skin" && i + 1 < argc) {
            try {
                charConfig.skinColor = static_cast<uint8_t>(std::stoul(argv[++i]));
            } catch (...) {}
        }
        else if (arg == "--face" && i + 1 < argc) {
            try {
                charConfig.faceType = static_cast<uint8_t>(std::stoul(argv[++i]));
            } catch (...) {}
        }
        else if (arg == "--hair" && i + 1 < argc) {
            try {
                charConfig.hairStyle = static_cast<uint8_t>(std::stoul(argv[++i]));
            } catch (...) {}
        }
        else if (arg == "--hair-color" && i + 1 < argc) {
            try {
                charConfig.hairColor = static_cast<uint8_t>(std::stoul(argv[++i]));
            } catch (...) {}
        }
        else if (arg == "--facial-hair" && i + 1 < argc) {
            try {
                charConfig.facialHair = static_cast<uint8_t>(std::stoul(argv[++i]));
            } catch (...) {}
        }
        else if (arg == "--equip" && i + 1 < argc) {
            parseEquipment(argv[++i], charConfig);
        }
        else {
            std::cerr << "Warning: Unknown argument: " << arg << "\n";
        }
    }

    // Validate required arguments
    if (dataPath.empty()) {
        std::cerr << "Error: Data path is required (-d/--data)\n";
        printUsage(argv[0]);
        return 1;
    }

    if (config.outputPath.empty()) {
        std::cerr << "Error: Output path is required (-o/--output)\n";
        printUsage(argv[0]);
        return 1;
    }

    if (config.mapName.empty() && config.mapId == 0 && !config.isWmoMap) {
        std::cerr << "Error: Map name, map ID, or WMO path is required (-m/--map, --map-id, or --wmo)\n";
        printUsage(argv[0]);
        return 1;
    }

    // Set character config if character enabled
    if (showCharacter) {
        config.character = charConfig;
    }

    // Initialize logging
    if (verbose) {
        core::Logger::getInstance().setLogLevel(core::LogLevel::DEBUG);
    } else if (quiet) {
        core::Logger::getInstance().setLogLevel(core::LogLevel::ERROR);
    } else {
        core::Logger::getInstance().setLogLevel(core::LogLevel::INFO);
    }

    // Print configuration
    if (!quiet) {
        std::cout << "=== WoW Scene Capture ===\n";
        std::cout << "Configuration:\n";
        std::cout << "  Output: " << config.outputPath << "\n";
        std::cout << "  Resolution: " << config.width << "x" << config.height << "\n";
        std::cout << "  Data path: " << dataPath << "\n";
        if (!installPath.empty()) {
            std::cout << "  Installation: " << installPath << "\n";
        }
        if (config.isWmoMap) {
            std::cout << "  WMO: " << config.mapName << "\n";
        } else {
            std::cout << "  Map: " << (config.mapName.empty() ? std::to_string(config.mapId) : config.mapName) << "\n";
        }
        std::cout << "  Camera: (" << config.cameraPosition.x << ", " << config.cameraPosition.y << ", " << config.cameraPosition.z << ")\n";
        if (config.useOrbitCamera) {
            std::cout << "  Target: (" << config.cameraTarget.x << ", " << config.cameraTarget.y << ", " << config.cameraTarget.z << ")\n";
        }
        if (showCharacter) {
            std::cout << "  Character: enabled\n";
        }
        std::cout << "\n";
    }

    // Initialize capture system
    SceneCapture capture;
    for (const auto& [key, value] : settingOverrides) {
        capture.addSettingOverride(key, value);
    }
    capture.setWireframe(wireframe);
    if (!capture.initialize(dataPath, installPath)) {
        std::cerr << "Error: " << capture.getLastError() << "\n";
        return 1;
    }

    // Execute capture
    if (!quiet) {
        std::cout << "Capturing scene...\n";
    }

    CaptureResult result = capture.capture(config);

    if (!result.success) {
        std::cerr << "Error: " << result.error << "\n";
        capture.shutdown();
        return 1;
    }

    if (!quiet) {
        std::cout << "\nCapture complete!\n";
        std::cout << "  Screenshot: " << result.screenshotPath << "\n";
        std::cout << "  Manifest: " << result.manifestPath << "\n";
        std::cout << "  Entities: " << result.entities.size() << "\n";
    }

    capture.shutdown();
    return 0;
}
