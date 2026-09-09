#pragma once

#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

/// An original game installation found on disk.
///
/// This is what lets `wowee.exe` be dropped next to the game's own executable
/// and read its archives where they lie. Nothing here extracts, converts, or
/// writes: it locates the installation, says which build and locale it is, and
/// lists its archives in load order.
struct GameInstall {
    std::string root;     ///< directory holding the game executable
    std::string dataDir;  ///< <root>/Data, the directory the archives live in
    /// "classic", "tbc", "wotlk" or "turtle"; empty when the data is not one
    /// of the supported builds.
    std::string expansion;
    std::string locale;   ///< "enUS", "deDE", ...; empty when no locale dir exists
    /// Absolute archive paths, lowest priority first. Empty for an invalid install.
    std::vector<std::string> archives;

    [[nodiscard]] bool isValid() const { return !archives.empty(); }
};

/// Archive names in load order, lowest priority first, relative to the Data
/// directory. Locale archives are named "<locale>/<file>.mpq" with the locale
/// lowercased; the caller resolves the real on-disk spelling.
///
/// Pure, so the order a build loads its archives in can be tested without an
/// installation present.
[[nodiscard]] std::vector<std::string> archiveSequence(const std::string& expansion,
                                                       const std::string& locale);

/// Which build the archives in @p dataDir belong to, or "" if not recognised.
[[nodiscard]] std::string detectExpansionAt(const std::string& dataDir);

/// The locale subdirectory present in @p dataDir, or "" if there is none.
[[nodiscard]] std::string detectLocaleAt(const std::string& dataDir);

/// Absolute paths of the archives present in @p dataDir, lowest priority first.
/// Names are matched case-insensitively; absent entries are skipped.
[[nodiscard]] std::vector<std::string> discoverArchives(const std::string& dataDir,
                                                        const std::string& expansion,
                                                        const std::string& locale);

/// Locate the installation to read from.
///
/// @param explicitPath a game root or Data directory named by the operator
///        (WOW_INSTALL_PATH). Tried first and, when set, alone: a named path
///        that holds no archives is a mistake worth seeing rather than a
///        reason to silently read some other installation.
///
/// Otherwise the directory holding this executable is tried, then its parent -
/// which is what makes `wowee.exe` beside `Wow.exe` work, and keeps it working
/// when the process is launched from an unrelated working directory - and the
/// working directory last.
[[nodiscard]] GameInstall detectGameInstall(const std::string& explicitPath = {});

}  // namespace pipeline
}  // namespace wowee
