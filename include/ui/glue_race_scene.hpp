#pragma once

// The scene the original glue screens stand a character in, for this client's
// own character screens.
//
// GlueParent.lua's SetBackgroundModel is the rule: the race names a model,
// Interface\Glues\Models\UI_<Race>\UI_<Race>.mdx, a death knight names
// UI_DeathKnight whatever the race, gnomes borrow the dwarves' scene and
// trolls the orcs' (its own "HACK" comment says so - Blizzard never shipped
// the other two), and CharModelFogInfo says what fog each carries. When the
// original GlueXML is loaded that Lua runs and records the same answer for
// the glue API to read back; this is the one place the client's own screens
// say it, so that they draw through the same path with the same descriptor.
//
// The renderer knows none of this. It draws whatever GlueSceneState it is
// handed, and this is only where the client's own screens get theirs.

#include "game/character.hpp"
#include "rendering/glue_scene.hpp"

namespace wowee::ui {

/// The descriptor GlueParent.lua would record for a character of `race` and
/// `characterClass`: model, camera 0, sequence 0, and the race's fog where
/// CharModelFogInfo has a row.
rendering::GlueSceneState glueRaceScene(game::Race race, game::Class characterClass);

} // namespace wowee::ui
