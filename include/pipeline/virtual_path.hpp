#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace wowee {
namespace pipeline {

/// Paths as the game's own archives spell them: backslash-separated,
/// case-insensitive, rooted at the data directory - "interface\\framexml\\ui.xml".
///
/// The original interface names its includes relative to the file including
/// them, and it does use "..": the guild bank's XML asks for
/// "..\\..\\FrameXML\\UIPanelTemplates.xml". A filesystem walks that itself;
/// an archive does not, and AssetManager::normalizePath refuses a path
/// containing ".." outright rather than escaping the data directory. So the
/// walk happens here, before the read, and what reaches the asset manager is
/// always a plain path.
///
/// Kept apart from AssetManager because it is string arithmetic with no
/// archive, filesystem or allocator behind it, and can be tested as such.

/// Lowercase, backslash-separated, with "." dropped and ".." applied.
/// A ".." that would climb above the root is dropped: there is nothing above
/// the data directory to name.
[[nodiscard]] inline std::string normalizeVirtual(const std::string& path) {
    std::vector<std::string> parts;
    std::string part;
    auto flush = [&] {
        if (part.empty() || part == ".") {
            part.clear();
            return;
        }
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
            part.clear();
            return;
        }
        parts.push_back(part);
        part.clear();
    };
    for (const char raw : path) {
        if (raw == '/' || raw == '\\') {
            flush();
            continue;
        }
        part.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(raw))));
    }
    flush();

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out.push_back('\\');
        out += parts[i];
    }
    return out;
}

/// The directory holding @p path, normalized. Empty when the path names
/// something at the root.
[[nodiscard]] inline std::string virtualParent(const std::string& path) {
    const std::string normalized = normalizeVirtual(path);
    const size_t at = normalized.find_last_of('\\');
    return at == std::string::npos ? std::string{} : normalized.substr(0, at);
}

/// The last component of @p path, normalized.
[[nodiscard]] inline std::string virtualBasename(const std::string& path) {
    const std::string normalized = normalizeVirtual(path);
    const size_t at = normalized.find_last_of('\\');
    return at == std::string::npos ? normalized : normalized.substr(at + 1);
}

/// @p relative resolved against directory @p dir, normalized.
[[nodiscard]] inline std::string joinVirtual(const std::string& dir,
                                             const std::string& relative) {
    if (dir.empty()) return normalizeVirtual(relative);
    return normalizeVirtual(dir + "\\" + relative);
}

/// Whether @p path ends with @p extension, which is given with its dot and in
/// lower case ("(.lua"). The interface spells its own file names every way -
/// FrameXML.toc names files as "Fonts.xml" and addons as "Foo.LUA" - so the
/// comparison cannot be case-sensitive.
[[nodiscard]] inline bool virtualHasExtension(const std::string& path,
                                              const std::string& extension) {
    if (path.size() < extension.size()) return false;
    for (size_t i = 0; i < extension.size(); ++i) {
        const char c = static_cast<char>(std::tolower(
            static_cast<unsigned char>(path[path.size() - extension.size() + i])));
        if (c != extension[i]) return false;
    }
    return true;
}

}  // namespace pipeline
}  // namespace wowee
