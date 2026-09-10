#pragma once

/**
 * render_caps_settings.hpp - the join between what the GPU can do and what the
 * settings panel offers.
 *
 * Its own header rather than a method on either side, because
 * `render_caps.hpp` must stay free of the settings schema (a test builds it
 * with no UI at all) and `settings_schema.cpp` must stay free of Vulkan.
 */

#include "rendering/render_caps.hpp"

namespace wowee {
namespace rendering {

/// Fill in `SettingDesc::unavailable` for every row this device cannot honour.
///
/// Called once, after the Vulkan device exists and before the settings panels
/// are first drawn. Calling it again with different caps replaces the previous
/// answer rather than adding to it.
void applyRenderCapsToSchema(const RenderCaps& caps);

}  // namespace rendering
}  // namespace wowee
