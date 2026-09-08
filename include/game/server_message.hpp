#pragma once

#include "network/packet.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace wowee::game {

struct ServerMessagePresentation {
    std::string chat;
    std::string uiError;
};

/// Decode the WotLK SMSG_CHAT_SERVER_MESSAGE body. AzerothCore writes the
/// ServerMessages.dbc id first and only writes StringParam for ids 1..3;
/// cancellation ids 4 and 5 are therefore valid four-byte packets.
[[nodiscard]] inline std::optional<ServerMessagePresentation>
parseServerMessage(network::Packet& packet) {
    if (!packet.hasRemaining(4)) return std::nullopt;

    const uint32_t type = packet.readUInt32();
    std::string parameter;
    if (packet.hasData()) {
        const auto& bytes = packet.getData();
        const auto begin = bytes.begin() + static_cast<std::ptrdiff_t>(packet.getReadPos());
        if (std::find(begin, bytes.end(), uint8_t{0}) == bytes.end()) return std::nullopt;
        parameter = packet.readString();
    }
    ServerMessagePresentation result;
    switch (type) {
        case 1: // SERVER_MSG_SHUTDOWN_TIME
            if (parameter.empty()) return std::nullopt;
            result.chat = "[Shutdown] " + parameter;
            result.uiError = "Server shutdown: " + parameter;
            break;
        case 2: // SERVER_MSG_RESTART_TIME
            if (parameter.empty()) return std::nullopt;
            result.chat = "[Restart] " + parameter;
            result.uiError = "Server restart: " + parameter;
            break;
        case 3: // SERVER_MSG_STRING
            if (parameter.empty()) return std::nullopt;
            result.chat = "[Server] " + parameter;
            break;
        case 4: // SERVER_MSG_SHUTDOWN_CANCELLED
            result.chat = "[Shutdown cancelled]";
            break;
        case 5: // SERVER_MSG_RESTART_CANCELLED
            result.chat = "[Restart cancelled]";
            break;
        default:
            if (parameter.empty()) return std::nullopt;
            result.chat = "[Server] " + parameter;
            break;
    }
    return result;
}

} // namespace wowee::game
