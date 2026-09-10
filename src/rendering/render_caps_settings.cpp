/**
 * render_caps_settings.cpp - which settings this GPU cannot honour, and why.
 *
 * One table, read once at start-up. It is the only place that turns a
 * capability flag into a sentence a player reads, and it is deliberately
 * separate from both halves it joins: `render_caps.hpp` knows nothing about
 * settings, and `settings_schema.cpp` knows nothing about Vulkan.
 *
 * The rule from docs/plan-modern-rendering.md §6.2: never a silent downgrade.
 * A setting the hardware cannot do is greyed with the reason, both panels show
 * that reason in the tooltip, and the write path refuses it. A setting that is
 * merely waiting on another setting is `enabledWhen` and not this.
 */

#include "rendering/render_caps_settings.hpp"

#include "core/logger.hpp"
#include "ui/settings_schema.hpp"

namespace wowee {
namespace rendering {

void applyRenderCapsToSchema(const RenderCaps& caps) {
    struct Gate {
        const char* key;
        bool available;
        const char* reason;
    };

    const Gate gates[] = {
        // Four cascades are four layers of one depth image. Every device that
        // has a Vulkan driver has at least 256 array layers, so this refuses
        // on nothing real - it is here because a capability with no gate is a
        // capability nobody notices has stopped being true.
        {"shadowcascades", caps.supportsCascadedShadows(),
         "This GPU cannot make a layered depth image large enough for cascades"},
    };

    for (const Gate& gate : gates) {
        ui::setSettingUnavailable(gate.key, gate.available ? "" : gate.reason);
        if (!gate.available) {
            LOG_WARNING("Setting '", gate.key, "' unavailable: ", gate.reason);
        }
    }
}

}  // namespace rendering
}  // namespace wowee
