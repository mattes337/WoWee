#include "core/test_input_trace.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

namespace wowee::core {
namespace {
using Json = nlohmann::json;
constexpr std::size_t kMaximumBytes = 1024 * 1024;

[[noreturn]] void invalid(const char* reason) {
    // Never append JSON values or parser exception text: traces may contain
    // password-field text, and diagnostics must not reproduce those payloads.
    throw std::invalid_argument(std::string("WOWEE_TEST_INPUT_TRACE: ") + reason);
}

int64_t integer(const Json& object, const char* key, int64_t low, int64_t high) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) invalid("missing or non-integer field");
    if (found->is_number_unsigned()) {
        const auto value = found->get<uint64_t>();
        if (value > static_cast<uint64_t>(high) || (low > 0 && value < static_cast<uint64_t>(low)))
            invalid("integer field out of range");
        return static_cast<int64_t>(value);
    }
    const auto value = found->get<int64_t>();
    if (value < low || value > high) invalid("integer field out of range");
    return value;
}

void fields(const Json& object, std::initializer_list<const char*> allowed) {
    if (!object.is_object()) invalid("expected an object");
    for (const auto& item : object.items()) {
        bool known = false;
        for (const char* key : allowed) if (item.key() == key) known = true;
        if (!known) invalid("unknown field");
    }
}
}

TestInputTrace TestInputTrace::fromFile(const char* path) {
    if (!path) return {};
    if (!*path) invalid("empty trace path");
    std::ifstream file(path, std::ios::binary);
    if (!file) invalid("trace file is missing or unreadable");
    std::string source;
    char buffer[4096];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        if (source.size() + static_cast<std::size_t>(file.gcount()) > kMaximumBytes)
            invalid("trace exceeds 1 MiB");
        source.append(buffer, static_cast<std::size_t>(file.gcount()));
    }
    if (file.bad()) invalid("trace file read failed");
    return parse(source);
}

TestInputTrace TestInputTrace::parse(std::string_view source) {
    if (source.size() > kMaximumBytes) invalid("trace exceeds 1 MiB");
    Json root;
    try { root = Json::parse(source.begin(), source.end()); }
    catch (const Json::exception&) { invalid("invalid JSON"); }
    fields(root, {"version", "stop_after_updates", "events"});
    if (integer(root, "version", 1, 1) != 1) invalid("unsupported version");
    TestInputTrace result;
    result.stopAfterUpdates_ = static_cast<uint32_t>(integer(root, "stop_after_updates", 1, 1000000));
    const auto entries = root.find("events");
    if (entries == root.end() || !entries->is_array() || entries->empty() || entries->size() > 10000)
        invalid("events must contain 1 to 10000 entries");
    for (const auto& entry : *entries) {
        if (!entry.is_object()) invalid("event must be an object");
        const auto type = entry.find("type");
        if (type == entry.end() || !type->is_string()) invalid("event type is missing");
        TestInputEvent event;
        event.afterUpdates = static_cast<uint32_t>(integer(entry, "after_updates", 0, result.stopAfterUpdates_ - 1));
        if (!result.events_.empty() && event.afterUpdates < result.events_.back().afterUpdates)
            invalid("events must be ordered by completed update");
        const std::string kind = type->get<std::string>();
        if (kind == "mouse_move") {
            fields(entry, {"type", "after_updates", "x", "y", "dx", "dy", "buttons"});
            event.kind = TestInputEvent::Kind::MouseMove;
            event.x = static_cast<int32_t>(integer(entry, "x", -1000000, 1000000));
            event.y = static_cast<int32_t>(integer(entry, "y", -1000000, 1000000));
            if (entry.contains("dx")) event.dx = static_cast<int32_t>(integer(entry, "dx", -1000000, 1000000));
            if (entry.contains("dy")) event.dy = static_cast<int32_t>(integer(entry, "dy", -1000000, 1000000));
            if (entry.contains("buttons")) event.buttons = static_cast<uint32_t>(integer(entry, "buttons", 0, 31));
        } else if (kind == "mouse_down" || kind == "mouse_up") {
            fields(entry, {"type", "after_updates", "x", "y", "button"});
            event.kind = kind == "mouse_down" ? TestInputEvent::Kind::MouseDown : TestInputEvent::Kind::MouseUp;
            event.x = static_cast<int32_t>(integer(entry, "x", -1000000, 1000000));
            event.y = static_cast<int32_t>(integer(entry, "y", -1000000, 1000000));
            event.button = static_cast<uint8_t>(integer(entry, "button", 1, 5));
        } else if (kind == "mouse_wheel") {
            fields(entry, {"type", "after_updates", "x", "y"});
            event.kind = TestInputEvent::Kind::MouseWheel;
            event.x = static_cast<int32_t>(integer(entry, "x", -1000, 1000));
            event.y = static_cast<int32_t>(integer(entry, "y", -1000, 1000));
        } else if (kind == "key_down" || kind == "key_up") {
            fields(entry, {"type", "after_updates", "keycode", "scancode", "modifiers"});
            event.kind = kind == "key_down" ? TestInputEvent::Kind::KeyDown : TestInputEvent::Kind::KeyUp;
            event.keycode = static_cast<int32_t>(integer(entry, "keycode", 1, 0x7fffffff));
            event.scancode = static_cast<uint16_t>(integer(entry, "scancode", 1, 511));
            if (entry.contains("modifiers")) event.modifiers = static_cast<uint16_t>(integer(entry, "modifiers", 0, 65535));
        } else if (kind == "text") {
            fields(entry, {"type", "after_updates", "text"});
            event.kind = TestInputEvent::Kind::Text;
            const auto text = entry.find("text");
            if (text == entry.end() || !text->is_string()) invalid("text event requires a string");
            event.text = text->get<std::string>();
            if (event.text.empty() || event.text.size() > 31 || event.text.find('\0') != std::string::npos)
                invalid("text event must contain 1 to 31 UTF-8 bytes without NUL");
        } else {
            invalid("unknown event type");
        }
        result.events_.push_back(std::move(event));
    }
    return result;
}

void TestInputTrace::queueDue(uint32_t completed, const std::function<int(const TestInputEvent&)>& push) {
    if (!enabled()) return;
    while (next_ < events_.size() && events_[next_].afterUpdates <= completed) {
        if (events_[next_].afterUpdates != completed)
            throw std::runtime_error("WOWEE_TEST_INPUT_TRACE: missed scheduled update");
        if (push(events_[next_]) != 1)
            throw std::runtime_error("WOWEE_TEST_INPUT_TRACE: SDL event queue rejected input");
        ++next_;
    }
}

void TestInputTrace::requireComplete(uint32_t completed) const {
    if (enabled() && (next_ != events_.size() || completed != stopAfterUpdates_))
        throw std::runtime_error("WOWEE_TEST_INPUT_TRACE: run ended before trace completion");
}

} // namespace wowee::core
