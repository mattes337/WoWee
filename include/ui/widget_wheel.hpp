#pragma once

#include <cstdint>
#include <functional>

namespace wowee::ui {

class WidgetTree;

/// Dispatch a wheel notch to the first wheel-enabled frame at the cursor or
/// above it. Returns whether the interface consumed the event. The callback
/// may mutate the tree; dispatch does not retain or revisit widget pointers
/// after invoking it.
bool dispatchWidgetWheel(WidgetTree& widgets, float x, float y, float delta,
                         const std::function<void(uint32_t, float)>& invoke);

} // namespace wowee::ui
