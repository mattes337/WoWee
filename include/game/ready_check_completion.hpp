#pragma once
#include "network/packet.hpp"
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace wowee::game {
// Counts only received answers. Sending FINISHED is a request; the server's
// FINISHED handler still owns the visible state transition.
class ReadyCheckCompletion {
public:
    void reset() { owned_ = false; sent_ = false; expected_.clear(); answered_.clear(); }
    void start(uint64_t self, uint64_t initiator, const std::vector<uint64_t>& roster) {
        reset();
        if (!self || self != initiator) return;
        expected_ = members(roster);
        expected_.insert(self);
        owned_ = expected_.size() > 1;
    }
    void confirm(uint64_t guid) {
        if (owned_ && expected_.contains(guid)) answered_.insert(guid);
    }
    // A changed roster invalidates ownership; it must not silently shrink the
    // expected set and finish an unanswered check.
    void validateRoster(uint64_t self, const std::vector<uint64_t>& roster, bool authorized) {
        auto current = members(roster);
        if (self) current.insert(self);
        if (!authorized || current != expected_) reset();
    }
    template<class Send>
    bool sendIfComplete(uint16_t opcode, Send&& send) {
        if (!owned_ || sent_ || opcode == 0xFFFF || answered_.size() != expected_.size()) return false;
        const network::Packet packet(opcode); // Empty client FINISHED body.
        send(packet); // If sending throws, no successful send is recorded.
        sent_ = true;
        return true;
    }
private:
    static std::unordered_set<uint64_t> members(const std::vector<uint64_t>& roster) {
        std::unordered_set<uint64_t> result;
        for (auto guid : roster) if (guid) result.insert(guid);
        return result;
    }
    bool owned_ = false;
    bool sent_ = false;
    std::unordered_set<uint64_t> expected_;
    std::unordered_set<uint64_t> answered_;
};
} // namespace wowee::game
