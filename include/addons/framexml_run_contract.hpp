#pragma once

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace wowee::addons {

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
