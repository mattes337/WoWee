#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace wowee {
namespace game {

/**
 * Identifies a WoW expansion for protocol/asset selection.
 */
struct ExpansionProfile {
    std::string id;            // "classic", "tbc", "wotlk", "cata"
    std::string name;          // "Wrath of the Lich King"
    std::string shortName;     // "WotLK"
    uint8_t majorVersion = 0;
    uint8_t minorVersion = 0;
    uint8_t patchVersion = 0;
    uint16_t build = 0;           // Realm build (sent in LOGON_CHALLENGE)
    uint16_t worldBuild = 0;      // World build (sent in CMSG_AUTH_SESSION, defaults to build)
    uint8_t protocolVersion = 0;  // SRP auth protocol version byte
    // Client header fields used in LOGON_CHALLENGE.
    // Defaults match a typical Windows x86 client.
    std::string game = "WoW";
    std::string platform = "x86";
    std::string os = "Win";
    std::string locale = "enUS";
    uint32_t timezone = 0;
    std::string dataPath;      // Absolute path to expansion data dir
    uint32_t maxLevel = 60;
    std::vector<uint32_t> races;
    std::vector<uint32_t> classes;

    /// The RSA public key this realm signs its Warden module with, 256 bytes.
    ///
    /// Empty means Blizzard's own, which is what a server running a genuine
    /// module uses. A server that builds its own signs it with a key of its
    /// own, and checking that signature against Blizzard's says only that the
    /// two differ. Written in the profile as 512 hex characters.
    std::vector<uint8_t> wardenRsaModulus;

    [[nodiscard]] std::string versionString() const;  // e.g. "3.3.5a"
};

/**
 * The expansion profiles built into the executable, for a run that has no
 * Data/expansions/ beside it at all - a wowee.exe dropped next to the original
 * game executable and nothing else. Disk is still read first; see
 * ExpansionRegistry::initialize.
 *
 * Both members are empty in a build that embeds nothing, and the registry then
 * has only the filesystem to look at.
 */
struct EmbeddedExpansions {
    /// The profile ids the store carries ("classic", "tbc", "wotlk", ...).
    std::function<std::vector<std::string>()> ids;
    /// Contents of one file, named by its path under the data root
    /// ("expansions/turtle/expansion.json"). False when it is not there.
    std::function<bool(const std::string& path, std::string& contents)> read;
};

/**
 * Scans Data/expansions/ for available expansion profiles and manages the active selection.
 */
class ExpansionRegistry {
public:
    /**
     * Scan dataRoot/expansions/ for expansion.json files.
     * @param dataRoot Path to Data/ directory (e.g. "./Data")
     * @param embedded Profiles carried inside the binary, used only when the
     *        scan found none - see the definition for why it is all or nothing
     * @return Number of profiles discovered
     */
    size_t initialize(const std::string& dataRoot,
                      const EmbeddedExpansions& embedded = {});

    /** All discovered profiles. */
    [[nodiscard]] const std::vector<ExpansionProfile>& getAllProfiles() const { return profiles_; }

    /** Lookup by id (e.g. "wotlk"). Returns nullptr if not found. */
    [[nodiscard]] const ExpansionProfile* getProfile(const std::string& id) const;

    /** Set the active expansion. Returns false if id not found. */
    bool setActive(const std::string& id);

    /** Get the active expansion profile. Never null after successful initialize(). */
    [[nodiscard]] const ExpansionProfile* getActive() const;

    /** Convenience: active expansion id. Empty if none. */
    [[nodiscard]] const std::string& getActiveId() const { return activeId_; }

private:
    std::vector<ExpansionProfile> profiles_;
    std::string activeId_;

    /// Told when activeId_ moves, by either of the two things that move it.
    /// The bank's shape follows the expansion and a slot number is what a move
    /// names, so anything that has to change with the expansion changes here.
    void activeChanged();

    bool loadProfile(const std::string& jsonPath, const std::string& dirPath);

    /// The body of one expansion.json, already read. @p sourceName is only
    /// what a rejected profile is named by in the log.
    bool parseProfile(const std::string& json, const std::string& dirPath,
                      const std::string& sourceName);
};

} // namespace game
} // namespace wowee
