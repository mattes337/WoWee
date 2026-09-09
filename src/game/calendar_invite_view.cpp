#include "game/calendar_invite_view.hpp"

#include <algorithm>
#include <cctype>

namespace wowee::game {
namespace {
std::string folded(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
}

void CalendarInviteViewState::reset() {
    eventId_ = selectedInviteId_ = selectedGuid_ = 0;
    hasSelection_ = false;
    criterion_.clear();
    reverse_ = false;
    order_.clear();
}

void CalendarInviteViewState::synchronize(
    uint64_t eventId, const std::vector<CalendarInviteViewRow>& rows) {
    if (eventId_ != eventId) reset();
    eventId_ = eventId;
    rebuild(rows);
    if (selectedDisplayIndex(rows) == 0) {
        selectedInviteId_ = 0;
        selectedGuid_ = 0;
        hasSelection_ = false;
    }
}

bool CalendarInviteViewState::sort(
    const std::string& criterion, bool reverse,
    const std::vector<CalendarInviteViewRow>& rows) {
    if (criterion != "name" && criterion != "class" && criterion != "status") return false;
    criterion_ = criterion;
    reverse_ = reverse;
    rebuild(rows);
    return true;
}

void CalendarInviteViewState::rebuild(const std::vector<CalendarInviteViewRow>& rows) {
    order_.clear();
    for (size_t i = 0; i < rows.size(); ++i) order_.push_back(i);
    if (criterion_.empty()) return;
    std::stable_sort(order_.begin(), order_.end(), [&](size_t leftIndex, size_t rightIndex) {
        const auto& left = rows[leftIndex];
        const auto& right = rows[rightIndex];
        int primary = 0;
        if (criterion_ == "name") {
            const auto l = folded(left.name), r = folded(right.name);
            const bool lu = l.empty(), ru = r.empty();
            if (lu != ru) return !lu;
            primary = l < r ? -1 : (r < l ? 1 : 0);
        } else if (criterion_ == "class") {
            const bool lu = left.classId == 0, ru = right.classId == 0;
            if (lu != ru) return !lu;
            primary = left.classId < right.classId ? -1 : (right.classId < left.classId ? 1 : 0);
        } else {
            primary = left.status < right.status ? -1 : (right.status < left.status ? 1 : 0);
        }
        return primary != 0 && (reverse_ ? primary > 0 : primary < 0);
    });
}

size_t CalendarInviteViewState::sourceIndex(
    size_t displayIndex, const std::vector<CalendarInviteViewRow>& rows) const {
    if (displayIndex == 0 || displayIndex > order_.size()) return 0;
    const size_t source = order_[displayIndex - 1];
    return source < rows.size() ? source + 1 : 0;
}

bool CalendarInviteViewState::select(
    size_t displayIndex, const std::vector<CalendarInviteViewRow>& rows) {
    const size_t source = sourceIndex(displayIndex, rows);
    if (source == 0) return false;
    selectedInviteId_ = rows[source - 1].inviteId;
    selectedGuid_ = rows[source - 1].guid;
    hasSelection_ = true;
    size_t matches = 0;
    for (const auto& row : rows)
        if (row.inviteId == selectedInviteId_ && row.guid == selectedGuid_) ++matches;
    if (matches != 1) {
        selectedInviteId_ = selectedGuid_ = 0;
        hasSelection_ = false;
        return false;
    }
    return true;
}

size_t CalendarInviteViewState::selectedDisplayIndex(
    const std::vector<CalendarInviteViewRow>& rows) const {
    if (!hasSelection_) return 0;
    size_t found = 0;
    for (size_t i = 0; i < order_.size(); ++i) {
        const size_t source = sourceIndex(i + 1, rows);
        if (source && rows[source - 1].inviteId == selectedInviteId_ &&
            rows[source - 1].guid == selectedGuid_) {
            if (found != 0) return 0;
            found = i + 1;
        }
    }
    return found;
}

} // namespace wowee::game
