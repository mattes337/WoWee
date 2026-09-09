#pragma once

#include <algorithm>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <glm/vec3.hpp>

namespace wowee {
namespace pipeline {
class AssetManager;
}

namespace audio {

/// How long a WAV runs, in seconds, read out of its own header. Zero when the
/// bytes are not a WAV whose length can be worked out.
///
/// Needed because this client's mixer has no looping one-shot: a sound that
/// should run continuously is re-triggered, and re-triggering it at the wrong
/// interval is either a gap of silence or the same track playing over itself.
/// The zone and city ambience get away with a fixed thirty seconds because
/// they are meant to be occasional; a glue screen's ambience is a loop.
///
/// Free and header-only so the arithmetic can be tested without a device.
[[nodiscard]] inline float wavDurationSeconds(const std::vector<uint8_t>& wav) {
    auto u16 = [&wav](size_t at) -> uint32_t {
        return static_cast<uint32_t>(wav[at]) | (static_cast<uint32_t>(wav[at + 1]) << 8);
    };
    auto u32 = [&wav](size_t at) -> uint32_t {
        return static_cast<uint32_t>(wav[at]) | (static_cast<uint32_t>(wav[at + 1]) << 8) |
               (static_cast<uint32_t>(wav[at + 2]) << 16) |
               (static_cast<uint32_t>(wav[at + 3]) << 24);
    };
    // "RIFF" .... "WAVE", then chunks.
    if (wav.size() < 44) return 0.0f;
    if (wav[0] != 'R' || wav[1] != 'I' || wav[2] != 'F' || wav[3] != 'F') return 0.0f;
    if (wav[8] != 'W' || wav[9] != 'A' || wav[10] != 'V' || wav[11] != 'E') return 0.0f;

    uint32_t byteRate = 0;
    size_t at = 12;
    // Walked rather than assumed to be at offset 36: a WAV written by a tool
    // that puts a LIST or fact chunk between fmt and data is still a WAV, and
    // reading the chunk that happens to sit at a fixed offset gives a length
    // wrong by whatever that chunk holds.
    while (at + 8 <= wav.size()) {
        const uint32_t chunkSize = u32(at + 4);
        const size_t body = at + 8;
        if (wav[at] == 'f' && wav[at + 1] == 'm' && wav[at + 2] == 't' && wav[at + 3] == ' ') {
            if (chunkSize < 16 || body + 16 > wav.size()) return 0.0f;
            byteRate = u32(body + 8);
            // Some encoders leave byteRate at zero; the three fields it is the
            // product of are also there.
            if (byteRate == 0) {
                const uint32_t channels = u16(body + 2);
                const uint32_t sampleRate = u32(body + 4);
                const uint32_t bits = u16(body + 14);
                byteRate = channels * sampleRate * (bits / 8);
            }
        } else if (wav[at] == 'd' && wav[at + 1] == 'a' && wav[at + 2] == 't' &&
                   wav[at + 3] == 'a') {
            if (byteRate == 0) return 0.0f;
            // The header may claim more than the file holds - a truncated
            // download, or a stream whose size was never filled in.
            const uint32_t have = static_cast<uint32_t>(
                std::min<size_t>(chunkSize, wav.size() - body));
            return static_cast<float>(have) / static_cast<float>(byteRate);
        }
        // Chunks are padded to an even length, and a zero-length one would
        // otherwise walk this loop forever.
        at = body + chunkSize + (chunkSize & 1u);
        if (chunkSize == 0) break;
    }
    return 0.0f;
}

class AmbientSoundManager {
public:
    AmbientSoundManager() = default;
    ~AmbientSoundManager() = default;

    // Initialization
    bool initialize(pipeline::AssetManager* assets);
    void shutdown();

    // Main update loop - called from renderer
    void update(float deltaTime, const glm::vec3& cameraPos, bool isIndoor, bool isSwimming = false, bool isBlacksmith = false);

    // Weather control
    enum class WeatherType { NONE, RAIN_LIGHT, RAIN_MEDIUM, RAIN_HEAVY, SNOW_LIGHT, SNOW_MEDIUM, SNOW_HEAVY };
    void setWeather(WeatherType type);
    [[nodiscard]] WeatherType getCurrentWeather() const { return currentWeather_; }

    // Zone ambience control
    enum class ZoneType {
        NONE,
        FOREST_NORMAL,
        FOREST_SNOW,
        BEACH,
        GRASSLANDS,
        JUNGLE,
        MARSH,
        DESERT_CANYON,
        DESERT_PLAINS
    };
    void setZoneType(ZoneType type);
    [[nodiscard]] ZoneType getCurrentZone() const { return currentZone_; }

    // Convenience: derive ZoneType and CityType from a WoW zone ID
    void setZoneId(uint32_t zoneId);

    // City ambience control
    enum class CityType {
        NONE,
        STORMWIND,
        IRONFORGE,
        DARNASSUS,
        ORGRIMMAR,
        UNDERCITY,
        THUNDERBLUFF
    };
    void setCityType(CityType type);
    [[nodiscard]] CityType getCurrentCity() const { return currentCity_; }

    // Emitter management
    enum class AmbientType {
        FIREPLACE_SMALL,
        FIREPLACE_LARGE,
        TORCH,
        FOUNTAIN,
        WATER_SURFACE,
        RIVER,
        WATERFALL,
        WIND,
        BIRD_DAY,
        CRICKET_NIGHT,
        OWL_NIGHT
    };

    uint64_t addEmitter(const glm::vec3& position, AmbientType type);

    // ---- The login and character screens' ambience -----------------------
    //
    // Its own pair rather than a ZoneType, because it is not a zone: the glue
    // screens name a SoundEntries row - GlueScreenIntro behind the login
    // screen, GlueScreenTauren behind a tauren - and want it running under the
    // screen for as long as the screen is up. Zone ambience is an occasional
    // sound in a place; this is a loop.

    /// Start looping `candidates`, the files a SoundEntries row lists, taking
    /// the first that reads. Asking for what is already playing does nothing,
    /// so the glue screens may say it on every show, which they do.
    ///
    /// `fadeSeconds` is how long it takes to come up to volume - four, from
    /// every caller in GlueXML.
    void setGlueAmbience(const std::vector<std::string>& candidates, float fadeSeconds,
                         pipeline::AssetManager* assets);
    void stopGlueAmbience();

    /// Pumped by whoever is drawing the glue screens. update() above is the
    /// world's and does not run there - the renderer's zone pass is what calls
    /// it, and there is no zone at the login screen.
    void updateGlueAmbience(float deltaTime);

    /// Which track is looping, or empty. For the client to report what it
    /// asked for on a machine where nothing can be heard.
    [[nodiscard]] const std::string& getGlueAmbienceTrack() const { return glueTrack_; }

    // Time of day control (0-24 hours)
    void setGameTime(float hours);

    // Volume control
    void setVolumeScale(float scale);
    [[nodiscard]] float getVolumeScale() const { return volumeScale_; }
    void setBellVolumeScale(float scale) { bellVolumeScale_ = scale; }
    [[nodiscard]] float getBellVolumeScale() const { return bellVolumeScale_; }

private:
    struct AmbientEmitter {
        uint64_t id;
        AmbientType type;
        glm::vec3 position;
        bool active;
        float lastPlayTime;
        float loopInterval;  // For periodic/looping sounds
    };

    struct AmbientSample {
        std::string path;
        std::vector<uint8_t> data;
        bool loaded;
    };

    // Sound libraries
    std::vector<AmbientSample> fireSoundsSmall_;
    std::vector<AmbientSample> fireSoundsLarge_;
    std::vector<AmbientSample> torchSounds_;
    std::vector<AmbientSample> waterSounds_;
    std::vector<AmbientSample> riverSounds_;
    std::vector<AmbientSample> waterfallSounds_;
    std::vector<AmbientSample> fountainSounds_;
    std::vector<AmbientSample> windSounds_;
    std::vector<AmbientSample> tavernSounds_;
    std::vector<AmbientSample> blacksmithSounds_;
    std::vector<AmbientSample> birdSounds_;
    std::vector<AmbientSample> cricketSounds_;

    // Weather sound libraries
    std::vector<AmbientSample> rainLightSounds_;
    std::vector<AmbientSample> rainMediumSounds_;
    std::vector<AmbientSample> rainHeavySounds_;
    std::vector<AmbientSample> snowLightSounds_;
    std::vector<AmbientSample> snowMediumSounds_;
    std::vector<AmbientSample> snowHeavySounds_;

    // Water ambience libraries
    std::vector<AmbientSample> underwaterSounds_;

    // Zone ambience libraries (day and night versions)
    std::vector<AmbientSample> forestNormalDaySounds_;
    std::vector<AmbientSample> forestNormalNightSounds_;
    std::vector<AmbientSample> forestSnowDaySounds_;
    std::vector<AmbientSample> forestSnowNightSounds_;
    std::vector<AmbientSample> beachDaySounds_;
    std::vector<AmbientSample> beachNightSounds_;
    std::vector<AmbientSample> grasslandsDaySounds_;
    std::vector<AmbientSample> grasslandsNightSounds_;
    std::vector<AmbientSample> jungleDaySounds_;
    std::vector<AmbientSample> jungleNightSounds_;
    std::vector<AmbientSample> marshDaySounds_;
    std::vector<AmbientSample> marshNightSounds_;
    std::vector<AmbientSample> desertCanyonDaySounds_;
    std::vector<AmbientSample> desertCanyonNightSounds_;
    std::vector<AmbientSample> desertPlainsDaySounds_;
    std::vector<AmbientSample> desertPlainsNightSounds_;

    // City ambience libraries (day and night versions)
    std::vector<AmbientSample> stormwindDaySounds_;
    std::vector<AmbientSample> stormwindNightSounds_;
    std::vector<AmbientSample> ironforgeSounds_;  // No separate day/night
    std::vector<AmbientSample> darnassusDaySounds_;
    std::vector<AmbientSample> darnassusNightSounds_;
    std::vector<AmbientSample> orgrimmarDaySounds_;
    std::vector<AmbientSample> orgrimmarNightSounds_;
    std::vector<AmbientSample> undercitySounds_;  // No separate day/night (underground)
    std::vector<AmbientSample> thunderbluffDaySounds_;
    std::vector<AmbientSample> thunderbluffNightSounds_;

    // City bell sounds
    std::vector<AmbientSample> bellAllianceSounds_;
    std::vector<AmbientSample> bellHordeSounds_;
    std::vector<AmbientSample> bellNightElfSounds_;
    std::vector<AmbientSample> bellTribalSounds_;

    // Active emitters
    std::vector<AmbientEmitter> emitters_;
    uint64_t nextEmitterId_ = 1;

    // State tracking
    float gameTimeHours_ = 12.0f;  // Default noon
    float volumeScale_ = 1.0f;
    float bellVolumeScale_ = 0.5f;
    float birdTimer_ = 0.0f;
    float cricketTimer_ = 0.0f;
    float windLoopTime_ = 0.0f;
    float blacksmithLoopTime_ = 0.0f;
    float weatherLoopTime_ = 0.0f;
    float oceanLoopTime_ = 0.0f;
    float zoneLoopTime_ = 0.0f;
    float cityLoopTime_ = 0.0f;
    float bellTollTime_ = 0.0f;
    bool wasIndoor_ = false;
    bool wasBlacksmith_ = false;
    bool wasSwimming_ = false;
    bool initialized_ = false;
    WeatherType currentWeather_ = WeatherType::NONE;
    uint32_t currentZoneId_ = 0;
    ZoneType currentZone_ = ZoneType::NONE;
    CityType currentCity_ = CityType::NONE;

    // Active audio tracking
    struct ActiveSound {
        uint64_t emitterId;
        float startTime;
    };
    std::vector<ActiveSound> activeSounds_;

    // The glue screens' loop. One track at a time: one glue screen is up at a
    // time and each names its own.
    AmbientSample glueSample_;
    /// The first file of the row last asked for, whether or not it is the one
    /// that loaded - which is what "already asked for" has to be keyed on.
    std::string glueRequest_;
    std::string glueTrack_;
    /// Seconds, read out of the wav's own header, so the re-trigger lands
    /// where the track ends rather than at a guessed interval.
    float glueDuration_ = 0.0f;
    float glueElapsed_ = 0.0f;
    float glueFadeSeconds_ = 0.0f;
    float glueFadeElapsed_ = 0.0f;
    uint32_t glueVoice_ = 0;

    // Helper methods
    void updatePositionalEmitters(float deltaTime, const glm::vec3& cameraPos);
    void updatePeriodicSounds(float deltaTime, bool isIndoor, bool isSwimming);
    void updateWindAmbience(float deltaTime, bool isIndoor);
    void updateBlacksmithAmbience(float deltaTime);
    void updateWeatherAmbience(float deltaTime, bool isIndoor);
    void updateWaterAmbience(float deltaTime, bool isSwimming);
    void updateZoneAmbience(float deltaTime, bool isIndoor);
    void updateCityAmbience(float deltaTime);
    void updateBellTolls(float deltaTime);
    bool loadSound(const std::string& path, AmbientSample& sample, pipeline::AssetManager* assets);

    // Time of day helpers
    [[nodiscard]] bool isDaytime() const { return gameTimeHours_ >= 6.0f && gameTimeHours_ < 20.0f; }
    [[nodiscard]] bool isNighttime() const { return !isDaytime(); }
};

} // namespace audio
} // namespace wowee
