#include <catch_amalgamated.hpp>
#include "game/ready_check_member.hpp"

using namespace wowee::game;

namespace {
GroupListData roster() {
    GroupListData group;
    group.leaderGuid = 100;
    GroupMember leader; leader.guid = 100; leader.name = "Leader";
    GroupMember self; self.guid = 200; self.name = "Self";
    GroupMember assistant; assistant.guid = 300; assistant.name = "Assistant";
    group.members = {leader, self, assistant};
    return group;
}
}

TEST_CASE("ready check initiator uses its GUID rather than the group leader", "[ready-check]") {
    const auto group = roster();
    const auto initiator = readyCheckMember(group, 200, 300);
    CHECK(initiator.name == "Assistant");
    CHECK(initiator.unit == "party2");
    CHECK(readyCheckMember(group, 200, 100).unit == "party1");
    CHECK(readyCheckMember(group, 200, 200).unit == "player");
    CHECK(readyCheckMember(group, 200, 200).name == "Self");
}

TEST_CASE("ready check confirmation resolves raid indices and an absent local roster entry", "[ready-check]") {
    auto group = roster();
    group.groupType = 2;
    CHECK(readyCheckMember(group, 200, 300).unit == "raid3");
    CHECK(readyCheckMember(group, 200, 100).unit == "raid1");
    CHECK(readyCheckMember(group, 200, 200).unit == "player");
    group.members.erase(group.members.begin() + 1);
    CHECK(readyCheckMember(group, 200, 200).unit == "player");
    CHECK(readyCheckMember(group, 200, 300).unit == "raid2");
    group.groupType = 1; // Vanilla raid encoding
    CHECK(readyCheckMember(group, 200, 300).unit == "raid2");
    CHECK(readyCheckMember(group, 200, 999).unit.empty());
    CHECK(readyCheckMember(group, 200, 999).name.empty());
    CHECK(readyCheckMember(group, 0, 0).unit.empty());
}
