#pragma once

#include <cstdint>
#include <unordered_map>

namespace wowee::game {

// Server-owned answers survive FINISHED so FrameXML can fade member icons.
// Dismissing the local question must not end the group's ready check.
class ReadyCheckState {
public:
    void start() { active_ = true; answers_.clear(); }
    void confirm(uint64_t guid, bool ready) {
        if (guid != 0) answers_[guid] = ready;
    }
    void finish() { active_ = false; }
    void reset() { active_ = false; answers_.clear(); }

    [[nodiscard]] const char* status(uint64_t guid) const {
        if (guid == 0) return nullptr;
        const auto answer = answers_.find(guid);
        if (answer != answers_.end()) return answer->second ? "ready" : "notready";
        return active_ ? "waiting" : nullptr;
    }

    [[nodiscard]] uint32_t count(bool ready) const {
        uint32_t result = 0;
        for (const auto& answer : answers_) if (answer.second == ready) ++result;
        return result;
    }

private:
    bool active_ = false;
    std::unordered_map<uint64_t, bool> answers_;
};

} // namespace wowee::game
