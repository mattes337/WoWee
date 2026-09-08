#include <catch_amalgamated.hpp>

#include "game/server_message.hpp"

using namespace wowee;

namespace {
network::Packet serverMessage(uint32_t type, const char* parameter = nullptr) {
    network::Packet packet;
    packet.writeUInt32(type);
    if (parameter) packet.writeString(parameter);
    return packet;
}

// Adapter for the mapped handler before this change: it threw the type away,
// required a nonempty string, and labelled every surviving packet announcement.
std::optional<std::string> oldMappedPresentation(network::Packet packet) {
    if (!packet.hasRemaining(4)) return std::nullopt;
    packet.readUInt32();
    const std::string text = packet.readString();
    if (text.empty()) return std::nullopt;
    return "[Announcement] " + text;
}
}

TEST_CASE("WotLK server message types preserve their wire meaning", "[chat][packet]") {
    struct Case {
        uint32_t type;
        const char* parameter;
        const char* chat;
        const char* error;
    };
    const Case cases[] = {
        {1, "10 Seconds.", "[Shutdown] 10 Seconds.", "Server shutdown: 10 Seconds."},
        {2, "1 Minute.", "[Restart] 1 Minute.", "Server restart: 1 Minute."},
        {3, "Maintenance soon", "[Server] Maintenance soon", ""},
        // AzerothCore deliberately writes no StringParam for these ids.
        {4, nullptr, "[Shutdown cancelled]", ""},
        {5, nullptr, "[Restart cancelled]", ""},
    };

    for (const auto& expected : cases) {
        CAPTURE(expected.type);
        auto packet = serverMessage(expected.type, expected.parameter);
        const auto oldPresentation = oldMappedPresentation(packet);
        auto actual = game::parseServerMessage(packet);
        REQUIRE(actual);
        CHECK(actual->chat == expected.chat);
        CHECK(actual->uiError == expected.error);
        CHECK(packet.getRemainingSize() == 0);
        CHECK(oldPresentation != std::optional<std::string>{expected.chat});
    }
}

TEST_CASE("truncated and unusable server messages are rejected", "[chat][packet]") {
    network::Packet truncated(0, std::vector<uint8_t>{1, 0, 0});
    CHECK_FALSE(game::parseServerMessage(truncated));

    auto missingTimer = serverMessage(1);
    CHECK_FALSE(game::parseServerMessage(missingTimer));

    network::Packet unterminated;
    unterminated.writeUInt32(3);
    const uint8_t text[] = {'n', 'o', 'n', 'u', 'l'};
    unterminated.writeBytes(text, sizeof(text));
    CHECK_FALSE(game::parseServerMessage(unterminated));

    auto future = serverMessage(99, "future text");
    auto fallback = game::parseServerMessage(future);
    REQUIRE(fallback);
    CHECK(fallback->chat == "[Server] future text");
    CHECK(fallback->uiError.empty());
}
