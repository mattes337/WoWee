#include "core/config_paths.hpp"

#include "core/logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

namespace wowee::core {

namespace fs = std::filesystem;

bool enterResourceRoot() {
    const char* root = std::getenv("WOWEE_RESOURCE_ROOT");
    if (!root || !*root) return true;

    std::error_code ec;
    const fs::path current = fs::current_path(ec);
    if (!ec && current == fs::path(root)) return true;

    fs::current_path(fs::path(root), ec);
    if (ec) {
        LOG_ERROR("Could not enter resource root ", root, ": ", ec.message());
        return false;
    }
    LOG_INFO("Working directory set to the resource root ", root,
             " (was ", ec ? "unknown" : current.string(), ")");
    return true;
}

namespace {

// Per-user config location: %APPDATA%\wowee on Windows, ~/.wowee elsewhere.
// This is the default (non-portable) home and the migration source.
std::string perUserConfigDir() {
#if defined(_WIN32)
    const char* appdata = std::getenv("APPDATA");
    return appdata ? std::string(appdata) + "\\wowee" : ".";
#else
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + "/.wowee" : ".";
#endif
}

}  // namespace

std::string getExecutableDir() {
#if defined(_WIN32)
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    // Grow if the path was truncated (GetModuleFileNameW does not report the
    // required size; a full buffer signals truncation).
    while (len == buf.size()) {
        buf.resize(buf.size() * 2);
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    }
    if (len == 0) return {};
    return fs::path(buf.substr(0, len)).parent_path().string();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    // buf includes a trailing NUL from the API; strip to the reported path.
    if (auto nul = buf.find('\0'); nul != std::string::npos) buf.resize(nul);
    return fs::path(buf).parent_path().string();
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf));
    if (len <= 0) return {};
    return fs::path(std::string(buf, static_cast<size_t>(len))).parent_path().string();
#endif
}

std::string resolveResourcePathIn(const std::string& relative,
                                  const std::string& workingDir,
                                  const std::string& executableDir) {
    if (relative.empty()) return relative;

    const fs::path rel(relative);
    // An absolute path is already an answer; anchoring it would be a lie.
    if (rel.is_absolute()) return relative;

    std::error_code ec;
    // The working directory first. Spelled exactly as the caller asked, so a
    // log line and an error message read the way they always have.
    if (!workingDir.empty()) {
        if (fs::exists(fs::path(workingDir) / rel, ec)) return relative;
    } else if (fs::exists(rel, ec)) {
        return relative;
    }

    // Then beside the executable. lexically_normal because the callers spell
    // some of these with a leading "./" and "<exe>/./Data" in a log line is
    // just noise.
    if (!executableDir.empty()) {
        const fs::path anchored = (fs::path(executableDir) / rel).lexically_normal();
        if (fs::exists(anchored, ec)) return anchored.string();
    }

    return relative;
}

std::string resolveResourcePath(const std::string& relative) {
    return resolveResourcePathIn(relative, {}, getExecutableDir());
}

std::string currentLogFilePath() {
    // Logger::ensureFile picks between "logs/<name>" beside the working
    // directory and a per-user directory, and keeps neither where anything
    // else can read it. Rather than duplicate that decision - which would rot
    // the moment the logger's changes - both candidates are probed and the one
    // that exists is named. When both do, the newer one is this run's.
    const char* named = std::getenv("WOWEE_LOG_FILE");
    const std::string logFile = (named && *named) ? named : "wowee.log";

    std::vector<fs::path> candidates;
    candidates.emplace_back(fs::path("logs") / logFile);
    if (const char* root = std::getenv("WOWEE_CONFIG_ROOT"); root && *root) {
        candidates.emplace_back(fs::path(root) / "logs" / logFile);
    }
#if defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
        candidates.emplace_back(fs::path(local) / "Wowee" / "logs" / logFile);
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home && *home) {
        candidates.emplace_back(fs::path(home) / "Library" / "Logs" / "Wowee" / logFile);
    }
#else
    if (const char* state = std::getenv("XDG_STATE_HOME"); state && *state) {
        candidates.emplace_back(fs::path(state) / "wowee" / "logs" / logFile);
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        candidates.emplace_back(fs::path(home) / ".local" / "state" / "wowee" / "logs" / logFile);
    }
#endif
    std::error_code ec;
    candidates.push_back(fs::temp_directory_path(ec) / "wowee-logs" / logFile);

    fs::path best;
    fs::file_time_type bestTime{};
    for (const auto& candidate : candidates) {
        if (!fs::is_regular_file(candidate, ec)) continue;
        const auto when = fs::last_write_time(candidate, ec);
        if (ec) { ec.clear(); continue; }
        if (best.empty() || when > bestTime) {
            best = candidate;
            bestTime = when;
        }
    }
    if (best.empty()) return {};
    const fs::path absolute = fs::absolute(best, ec);
    return ec ? best.string() : absolute.string();
}

std::string getConfigRoot() {
    std::error_code ec;
    // An explicit root wins over everything.
    //
    // The tools write here too - the missing-API list and the Lua error list
    // are both rewritten on exit - so a headless run beside a real session
    // overwrote the two files a bug report is read from. A harness needs its
    // own corner rather than the player's.
    if (const char* root = std::getenv("WOWEE_CONFIG_ROOT"); root && *root) {
        fs::create_directories(root, ec);
        return root;
    }
    const std::string exeDir = getExecutableDir();
    if (!exeDir.empty()) {
        const fs::path portableMarker = fs::path(exeDir) / "portable.txt";
        const fs::path portableDir = fs::path(exeDir) / "config";
        // Either the opt-in marker file, or a config folder created by a prior
        // portable run, keeps config folder-local.
        if (fs::exists(portableMarker, ec) || fs::is_directory(portableDir, ec)) {
            return portableDir.string();
        }
    }
    return perUserConfigDir();
}

void migratePortableConfigIfNeeded() {
    std::error_code ec;
    const std::string exeDir = getExecutableDir();
    if (exeDir.empty()) return;

    const fs::path marker = fs::path(exeDir) / "portable.txt";
    const fs::path portableDir = fs::path(exeDir) / "config";

    // Migration only applies the first time the user opts in via the marker.
    // Once the portable config folder exists it is the established store, so
    // there is nothing to seed and later launches copy nothing.
    if (!fs::exists(marker, ec)) return;
    if (fs::exists(portableDir, ec)) return;

    const fs::path userDir = perUserConfigDir();
    if (userDir.empty() || !fs::is_directory(userDir, ec)) {
        // Nothing to migrate; create the folder so portable mode is established
        // and this check does not repeat every launch.
        fs::create_directories(portableDir, ec);
        return;
    }

    fs::create_directories(portableDir, ec);
    fs::copy(userDir, portableDir,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    if (ec) {
        LOG_WARNING("Portable config migration from ", userDir.string(), " to ",
                    portableDir.string(), " failed: ", ec.message());
    } else {
        LOG_INFO("Migrated existing config from ", userDir.string(),
                 " into portable folder ", portableDir.string());
    }
}

}  // namespace wowee::core
