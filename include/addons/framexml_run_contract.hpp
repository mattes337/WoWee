#pragma once

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace wowee::addons {

struct RunnerViewport {
    int width = 1920;
    int height = 1080;
};

// Reject malformed and impractically large viewports before loading addons.
// Keep the destination unchanged if either dimension is invalid.
inline bool parseRunnerViewport(std::string_view text, RunnerViewport& output) {
    const auto separator = text.find('x');
    if (separator == std::string_view::npos) return false;
    const auto dimension = [](std::string_view part, int& value) {
        if (part.empty()) return false;
        const auto result = std::from_chars(part.data(), part.data() + part.size(), value);
        return result.ec == std::errc{} && result.ptr == part.data() + part.size()
            && value >= 1 && value <= 16384;
    };
    RunnerViewport parsed;
    if (!dimension(text.substr(0, separator), parsed.width)
        || !dimension(text.substr(separator + 1), parsed.height)) return false;
    output = parsed;
    return true;
}

inline bool hasLuaExpression(std::string_view text) {
    return text.find_first_not_of(" \t\r\n") != std::string_view::npos;
}

inline bool readableRunnerScript(const std::string& path) {
    std::ifstream script(path, std::ios::binary);
    if (!script) return false;
    const std::string text((std::istreambuf_iterator<char>(script)), std::istreambuf_iterator<char>());
    return !script.bad() && hasLuaExpression(text);
}

// Every phase contributes: even a run with no command-line expressions must
// fail if the interface never loaded, an addon failed, or a callback raised.
inline int frameXmlRunExitCode(bool loaded, bool haveAssets,
                               std::size_t luaErrors, std::size_t addonFailures,
                               int failedCommands) {
    const auto errors = std::min<std::size_t>(100, luaErrors);
    const auto addons = std::min<std::size_t>(100, addonFailures);
    const auto commands = std::clamp(failedCommands, 0, 100);
    return std::min(100, static_cast<int>(errors + addons) + commands +
                         (loaded ? 0 : 1) + (haveAssets ? 0 : 1));
}

} // namespace wowee::addons
