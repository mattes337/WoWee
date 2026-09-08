#include "ui/widget_wheel.hpp"

#include "ui/widget_tree.hpp"

namespace wowee::ui {

bool dispatchWidgetWheel(WidgetTree& widgets, float x, float y, float delta,
                         const std::function<void(uint32_t, float)>& invoke) {
    const float scale = widgets.uiScale();
    if (scale > 0.0f) {
        x /= scale;
        y /= scale;
    }

    // FrameXML expects one signed notch: stock hybridscrollframe.lua compares
    // upward movement with exactly 1. Camera fallback keeps the raw magnitude.
    delta = delta > 0.0f ? 1.0f : (delta < 0.0f ? -1.0f : 0.0f);
    if (delta == 0.0f) return false;

    // The child under the cursor may not own the handler. Walk to the first
    // ancestor that enabled wheel input, as scroll-frame templates require.
    uint32_t widget = widgets.hitTestWheel(x, y);
    while (widget != 0) {
        const auto* candidate = widgets.get(widget);
        if (!candidate) break;
        if (candidate->wheelEnabled) {
            // Return immediately because a Lua callback may mutate the tree.
            invoke(widget, delta);
            return true;
        }
        widget = candidate->parent;
    }
    return false;
}

} // namespace wowee::ui
