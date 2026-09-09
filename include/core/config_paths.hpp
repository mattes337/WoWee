#pragma once

#include <string>

namespace wowee::core {

// Absolute path to the directory holding the running executable.
// Empty if it cannot be determined.
std::string getExecutableDir();

// Anchor a resource path that the tree spells relative to the working
// directory - "assets/Wowee.png", "./Data", "addons" - so it is still found
// when the process was launched from somewhere else entirely.
//
// The working directory is tried first and wins when the file is there. That
// is deliberate, and it is the rule the rest of this branch already follows:
// VkShaderModule::loadFromFile reads assets/shaders/ before the copy embedded
// in the binary, and the three expansion table loaders read Data/expansions/
// before the embedded profiles. A developer running out of a checkout is
// editing the files in it and expects them to be the ones read.
//
// The executable's own directory is tried second, and that is what makes a
// wowee.exe dropped beside the original game executable work: Windows does not
// chdir on launch, so a double-click in the game folder - or a shortcut with
// any "start in" at all - leaves every relative path resolving against the
// wrong directory.
//
// Returns @p relative unchanged when neither has it, so a failure is still
// reported against the path the caller asked for rather than against a guess.
std::string resolveResourcePath(const std::string& relative);

// The same rule with both roots named, so it can be tested without moving the
// test binary or the process working directory. @p workingDir or
// @p executableDir may be empty, which skips that root.
std::string resolveResourcePathIn(const std::string& relative,
                                  const std::string& workingDir,
                                  const std::string& executableDir);

// Path of the log file this process is writing, or "" when none was found.
//
// Only used to name it in a startup error box: a failure the user can see is
// worth little if the detail behind it is in a file they cannot find. This
// probes for the file rather than asking the logger, which does not expose the
// path it chose - see the comment on the implementation.
std::string currentLogFilePath();

// Root directory for user config (login.cfg, settings.cfg, last_character.cfg,
// characters/). Two modes:
//   - Portable: if a "portable.txt" marker file, or an existing "config" folder,
//     sits next to the executable, config lives in <exe_dir>/config. This keeps
//     the whole client self-contained in one folder (USB sticks, clean uninstall,
//     easy backup of server profiles).
//   - Per-user (default): %APPDATA%\wowee on Windows, ~/.wowee elsewhere.
std::string getConfigRoot();

// One-time seeding of portable config. On the first launch after the user drops
// a "portable.txt" marker next to the executable (before any config folder
// exists), copies the existing per-user config tree into <exe_dir>/config so
// saved server profiles, settings, and characters carry over. No-op afterwards,
// and a no-op when not in portable mode. Call once at startup before config is
// read.
void migratePortableConfigIfNeeded();

// Enters the directory named by WOWEE_RESOURCE_ROOT, which holds assets/ and
// Data/ in the layout a desktop install has.
//
// Only Android sets that variable, and only Android needs this: a process there
// starts in a directory holding neither, and shaders, interface art and the
// expansion profiles are all opened through relative paths. It is not enough to
// do this once at startup, because SDL and the Vulkan driver leave the working
// directory at /system/bin, so this is called again once they are up.
//
// A no-op wherever WOWEE_RESOURCE_ROOT is unset. Returns false only if the
// variable names a directory that cannot be entered.
bool enterResourceRoot();

}  // namespace wowee::core
