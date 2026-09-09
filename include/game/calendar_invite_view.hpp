#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wowee::game {

struct CalendarInviteViewRow {
    uint64_t inviteId = 0;
    uint64_t guid = 0;
    std::string name;
    uint8_t classId = 0;
    uint8_t status = 0;
};

class CalendarInviteViewState {
public:
    void reset();
    void synchronize(uint64_t eventId, const std::vector<CalendarInviteViewRow>& rows);
    bool sort(const std::string& criterion, bool reverse,
              const std::vector<CalendarInviteViewRow>& rows);
    bool select(size_t displayIndex, const std::vector<CalendarInviteViewRow>& rows);
    [[nodiscard]] size_t selectedDisplayIndex(
        const std::vector<CalendarInviteViewRow>& rows) const;
    [[nodiscard]] size_t sourceIndex(size_t displayIndex,
                                     const std::vector<CalendarInviteViewRow>& rows) const;
    [[nodiscard]] const std::string& criterion() const { return criterion_; }
    [[nodiscard]] bool reverse() const { return reverse_; }

private:
    void rebuild(const std::vector<CalendarInviteViewRow>& rows);
    uint64_t eventId_ = 0;
    uint64_t selectedInviteId_ = 0;
    uint64_t selectedGuid_ = 0;
    bool hasSelection_ = false;
    std::string criterion_;
    bool reverse_ = false;
    std::vector<size_t> order_;
};

} // namespace wowee::game
