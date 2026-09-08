#pragma once

#include "game/group_defines.hpp"

namespace wowee::game {

struct ReadyCheckMember {
    std::string unit;
    std::string name;
};

// Match the party/raid indices exposed by the Lua unit resolver. Names and
// tokens come from the roster even when a member has no nearby world entity.
inline ReadyCheckMember readyCheckMember(const GroupListData& group,
                                        uint64_t playerGuid, uint64_t guid) {
    if (guid == 0) return {};
    ReadyCheckMember result;
    if (guid == playerGuid) result.unit = "player";
    unsigned partyIndex = 0;
    for (std::size_t i = 0; i < group.members.size(); ++i) {
        const auto& member = group.members[i];
        if (member.guid != playerGuid) ++partyIndex;
        if (member.guid != guid) continue;
        result.name = member.name;
        if (result.unit.empty()) {
            result.unit = groupIsRaid(group.groupType)
                ? "raid" + std::to_string(i + 1)
                : "party" + std::to_string(partyIndex);
        }
        break;
    }
    return result;
}

} // namespace wowee::game
