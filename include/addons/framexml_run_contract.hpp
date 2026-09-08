#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

namespace wowee::addons {

struct RunnerViewport {
    int width = 1920;
    int height = 1080;
};

struct RunnerPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct RunnerMouse {
    RunnerPoint point;
    std::string buttons;
};

inline bool validRunnerUtf8(std::string_view text) {
    if (text.empty()) return false;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t count = 0;
        uint32_t codepoint = 0;
        if (lead <= 0x7f) { count = 1; codepoint = lead; }
        else if (lead >= 0xc2 && lead <= 0xdf) { count = 2; codepoint = lead & 0x1f; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 3; codepoint = lead & 0x0f; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 4; codepoint = lead & 0x07; }
        else return false;
        if (i + count > text.size()) return false;
        for (std::size_t j = 1; j < count; ++j) {
            const auto continuation = static_cast<unsigned char>(text[i + j]);
            if ((continuation & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if ((count == 3 && codepoint < 0x800) || (count == 4 && codepoint < 0x10000)
            || (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) return false;
        i += count;
    }
    return true;
}

inline bool parseRunnerKey(std::string_view text, int& output) {
    constexpr std::array<std::pair<std::string_view, int>, 11> keys{{
        {"BACKSPACE", '\b'}, {"DELETE", 0x4000004c}, {"LEFT", 0x40000050},
        {"RIGHT", 0x4000004f}, {"HOME", 0x4000004a}, {"END", 0x4000004d},
        {"UP", 0x40000052}, {"DOWN", 0x40000051}, {"ENTER", '\r'},
        {"ESCAPE", 27}, {"TAB", '\t'},
    }};
    const auto found = std::find_if(keys.begin(), keys.end(),
        [text](const auto& key) { return key.first == text; });
    if (found == keys.end()) return false;
    output = found->second;
    return true;
}

inline bool parseRunnerCoordinate(std::string_view text, float& output) {
    if (text.empty()) return false;
    float parsed = 0.0f;
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        parsed, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || !std::isfinite(parsed)) return false;
    output = parsed;
    return true;
}

inline bool parseRunnerPoint(std::string_view text, RunnerPoint& output) {
    const auto separator = text.find(',');
    if (separator == std::string_view::npos
        || text.find(',', separator + 1) != std::string_view::npos) return false;
    RunnerPoint parsed;
    if (!parseRunnerCoordinate(text.substr(0, separator), parsed.x)
        || !parseRunnerCoordinate(text.substr(separator + 1), parsed.y)) return false;
    output = parsed;
    return true;
}

inline bool parseRunnerMouse(std::string_view text, RunnerMouse& output) {
    const auto buttonsSeparator = text.rfind(',');
    if (buttonsSeparator == std::string_view::npos) return false;
    RunnerMouse parsed;
    if (!parseRunnerPoint(text.substr(0, buttonsSeparator), parsed.point)) return false;
    const auto buttons = text.substr(buttonsSeparator + 1);
    if (buttons.find_first_not_of("LRM") != std::string_view::npos) return false;
    parsed.buttons.assign(buttons);
    output = std::move(parsed);
    return true;
}

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
