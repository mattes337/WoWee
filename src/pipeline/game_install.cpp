#include "pipeline/game_install.hpp"

#include "core/config_paths.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace wowee {
namespace pipeline {

namespace fs = std::filesystem;

namespace {

std::string toLowerStr(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Known WoW client locales
const std::vector<std::string> kKnownLocales = {
    "enUS", "enGB", "deDE", "frFR", "esES", "esMX",
    "ruRU", "koKR", "zhCN", "zhTW", "ptBR"
};

// Whether a small text file under @p directory contains @p needle, matched
// without regard to case. Used to read a client's own realm list, which is a
// few dozen bytes.
bool fileMentions(const fs::path& directory, const std::string& fileName,
                  const std::string& needle) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return false;
    const std::string wantedName = toLowerStr(fileName);
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (toLowerStr(entry.path().filename().string()) != wantedName) continue;
        if (fs::file_size(entry.path(), ec) > 64 * 1024 || ec) return false;
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) return false;
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        return toLowerStr(text).find(toLowerStr(needle)) != std::string::npos;
    }
    return false;
}

bool hasFileCaseInsensitive(const fs::path& directory, const std::string& expectedName) {
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return false;
    const std::string expected = toLowerStr(expectedName);
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) break;
        if (entry.is_regular_file() &&
            toLowerStr(entry.path().filename().string()) == expected) {
            return true;
        }
    }
    return false;
}

std::string findCaseInsensitiveDirectory(const std::string& parentDir,
                                         const std::string& directoryName) {
    std::error_code ec;
    if (!fs::is_directory(parentDir, ec)) return "";
    const std::string lowerDirectoryName = toLowerStr(directoryName);
    for (const auto& entry : fs::directory_iterator(parentDir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (toLowerStr(name) == lowerDirectoryName) {
            return name;
        }
    }
    return "";
}

// Lowercased archive filename -> its real on-disk spelling.
std::unordered_map<std::string, std::string> buildCaseMap(const std::string& dir) {
    std::unordered_map<std::string, std::string> map;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return map;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string filename = entry.path().filename().string();
        if (filename.rfind("._", 0) == 0) {
            continue;
        }
        if (toLowerStr(entry.path().extension().string()) == ".mpq") {
            map[toLowerStr(filename)] = filename;
        }
    }
    return map;
}

}  // namespace

std::vector<std::string> archiveSequence(const std::string& expansion,
                                         const std::string& locale) {
    const std::string lowerLocale = toLowerStr(locale);

    std::vector<std::string> baseSequence;
    std::vector<std::string> localeSequence;

    if (expansion == "classic" || expansion == "turtle") {
        baseSequence = {
            "base.mpq", "backup.mpq", "dbc.mpq", "fonts.mpq",
            "interface.mpq", "misc.mpq", "model.mpq", "sound.mpq",
            "speech.mpq", "terrain.mpq", "texture.mpq", "wmo.mpq"
        };
    } else if (expansion == "tbc") {
        baseSequence = { "common.mpq", "expansion.mpq" };
        if (!locale.empty()) {
            localeSequence = {
                lowerLocale + "/backup-" + lowerLocale + ".mpq",
                lowerLocale + "/base-" + lowerLocale + ".mpq",
                lowerLocale + "/locale-" + lowerLocale + ".mpq",
                lowerLocale + "/speech-" + lowerLocale + ".mpq",
                lowerLocale + "/expansion-locale-" + lowerLocale + ".mpq",
                lowerLocale + "/expansion-speech-" + lowerLocale + ".mpq",
            };
        }
    } else {
        baseSequence = { "common.mpq", "common-2.mpq", "expansion.mpq", "lichking.mpq" };
        if (!locale.empty()) {
            localeSequence = {
                lowerLocale + "/backup-" + lowerLocale + ".mpq",
                lowerLocale + "/base-" + lowerLocale + ".mpq",
                lowerLocale + "/locale-" + lowerLocale + ".mpq",
                lowerLocale + "/speech-" + lowerLocale + ".mpq",
                lowerLocale + "/expansion-locale-" + lowerLocale + ".mpq",
                lowerLocale + "/expansion-speech-" + lowerLocale + ".mpq",
                lowerLocale + "/lichking-locale-" + lowerLocale + ".mpq",
                lowerLocale + "/lichking-speech-" + lowerLocale + ".mpq",
            };
        }
    }

    std::vector<std::string> sequence = baseSequence;
    sequence.insert(sequence.end(), localeSequence.begin(), localeSequence.end());

    // Interleave patches: base patch then locale patch for each tier
    std::vector<std::string> patchSuffixes = {""};
    for (int i = 2; i <= 9; ++i) {
        patchSuffixes.push_back(std::string("-") + std::to_string(i));
    }
    for (char c = 'a'; c <= 'z'; ++c) {
        patchSuffixes.push_back(std::string("-") + c);
    }

    for (const auto& suffix : patchSuffixes) {
        sequence.push_back("patch" + suffix + ".mpq");
        if (!locale.empty()) {
            sequence.push_back(lowerLocale + "/patch-" + lowerLocale + suffix + ".mpq");
        }
    }

    return sequence;
}

std::string detectExpansionAt(const std::string& dataDir) {
    if (hasFileCaseInsensitive(dataDir, "lichking.mpq"))
        return "wotlk";
    if (hasFileCaseInsensitive(dataDir, "expansion.mpq"))
        return "tbc";
    // Turtle WoW uses vanilla-era base MPQs, so detect its executable and its
    // custom high-numbered/letter patch archives before falling back to Classic.
    if (hasFileCaseInsensitive(dataDir, "dbc.mpq") ||
        hasFileCaseInsensitive(dataDir, "terrain.mpq")) {
        const fs::path clientRoot = fs::path(dataDir).parent_path();
        if (hasFileCaseInsensitive(clientRoot, "TurtleWoW.exe")) return "turtle";
        // The installation on hand is a Turtle client whose launcher is the
        // stock WoW.exe beside VanillaFixes, with patch-3 and patch-4 rather
        // than the lettered archives below - so neither existing signal fires
        // and it read as plain Vanilla, which is the wrong auth build.
        //
        // Its own realm list names the realm it was shipped for, and a Vanilla
        // client pointed at a Vanilla server does not. Read rather than
        // guessed at, because the alternative - treating any patch tier past
        // stock Vanilla's patch-2 as Turtle - would call every community patch
        // Turtle and send auth build 7272 to a server expecting 5875.
        if (fileMentions(clientRoot, "realmlist.wtf", "turtle-wow") ||
            fileMentions(clientRoot / "WTF", "Config.wtf", "turtle-wow")) {
            return "turtle";
        }
        for (int patch = 8; patch <= 9; ++patch) {
            if (hasFileCaseInsensitive(dataDir,
                                       "patch-" + std::to_string(patch) + ".mpq")) {
                return "turtle";
            }
        }
        for (char c = 'a'; c <= 'z'; ++c) {
            if (hasFileCaseInsensitive(dataDir, std::string("patch-") + c + ".mpq")) {
                return "turtle";
            }
        }
        return "classic";
    }
    return "";
}

std::string detectLocaleAt(const std::string& dataDir) {
    std::error_code ec;
    if (!fs::is_directory(dataDir, ec)) return "";
    for (const auto& entry : fs::directory_iterator(dataDir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        std::string lower = toLowerStr(name);
        for (const auto& loc : kKnownLocales) {
            if (toLowerStr(loc) == lower) {
                return name;
            }
        }
    }
    return "";
}

std::vector<std::string> discoverArchives(const std::string& dataDir,
                                          const std::string& expansion,
                                          const std::string& locale) {
    std::vector<std::string> result;

    auto caseMap = buildCaseMap(dataDir);
    const std::string lowerLocale = toLowerStr(locale);
    if (!locale.empty()) {
        std::string actualLocaleDir = findCaseInsensitiveDirectory(dataDir, locale);
        if (actualLocaleDir.empty()) {
            actualLocaleDir = locale;
        }
        const fs::path localeDirPath = fs::path(dataDir) / actualLocaleDir;
        for (auto& [name, realName] : buildCaseMap(localeDirPath.string())) {
            caseMap[lowerLocale + "/" + name] = (fs::path(actualLocaleDir) / realName).string();
        }
    }

    for (const auto& expected : archiveSequence(expansion, locale)) {
        auto it = caseMap.find(toLowerStr(expected));
        if (it != caseMap.end()) {
            result.push_back((fs::path(dataDir) / it->second).string());
        }
    }

    return result;
}

namespace {

// Fill in an install rooted at @p candidate, which may be either a game root
// (holding Data/) or the Data directory itself. Returns an invalid install
// when no archives of a supported build are there.
GameInstall inspectCandidate(const fs::path& candidate) {
    GameInstall install;
    std::error_code ec;
    if (candidate.empty() || !fs::is_directory(candidate, ec)) return install;

    fs::path dataDir;
    const std::string dataName = findCaseInsensitiveDirectory(candidate.string(), "Data");
    if (!dataName.empty()) {
        dataDir = candidate / dataName;
    } else if (!detectExpansionAt(candidate.string()).empty()) {
        // Pointed straight at a Data directory.
        dataDir = candidate;
    } else {
        return install;
    }

    install.expansion = detectExpansionAt(dataDir.string());
    if (install.expansion.empty()) return install;

    install.root = (dataDir == candidate ? candidate.parent_path() : candidate).string();
    install.dataDir = dataDir.string();
    install.locale = detectLocaleAt(dataDir.string());
    install.archives = discoverArchives(dataDir.string(), install.expansion, install.locale);
    return install;
}

}  // namespace

GameInstall detectGameInstall(const std::string& explicitPath) {
    if (!explicitPath.empty()) {
        return inspectCandidate(fs::path(explicitPath));
    }

    std::vector<fs::path> candidates;
    const std::string exeDir = core::getExecutableDir();
    if (!exeDir.empty()) {
        candidates.emplace_back(exeDir);
        candidates.push_back(fs::path(exeDir).parent_path());
    }
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec) {
        candidates.push_back(cwd);
        candidates.push_back(cwd.parent_path());
    }

    for (const auto& candidate : candidates) {
        GameInstall install = inspectCandidate(candidate);
        if (install.isValid()) return install;
    }
    return {};
}

}  // namespace pipeline
}  // namespace wowee
