#pragma once

#include "game/character.hpp"

#include <cstdint>

namespace wowee::addons {

/// The selection the original login and character screens are holding.
///
/// Their own state rather than the client's: which row of the character list
/// is highlighted and which race button is pressed are questions only the
/// screen showing them can answer, and those screens ask for them back on
/// every redraw. The character actually being entered with is set on the world
/// handler as it is chosen, so nothing downstream reads this.
///
/// In a header because two places need it: the glue API writes it, and the
/// backdrop pass reads it to know which figure to stand in the scene. It was a
/// file-static in lua_glue_api.cpp while nothing drew the figure.
struct GlueSelection {
    int characterIndex = 0;     ///< 1-based row of the character list, 0 for none
    int raceIndex      = 1;     ///< 1-based row of GetAvailableRaces
    /// The race that row resolves to, written by the glue API's selectedRace()
    /// as a side effect of the read every handler already makes.
    ///
    /// Kept here because resolving a row needs the available-race list, and
    /// that list needs the expansion profile off the Lua services - which the
    /// backdrop pass has no handle on. Storing the answer is cheaper and less
    /// fragile than a second way to compute it.
    game::Race race = game::Race::HUMAN;
    int classIndex     = 1;     ///< 1-based row of GetAvailableClasses
    game::Gender gender = game::Gender::MALE;
    uint8_t skin = 0, face = 0, hairStyle = 0, hairColor = 0, facialHair = 0;
    /// Where the camera is around the model, in degrees.
    float selectFacing = 0.0f;
    float createFacing = 0.0f;
    /// Everything about the figure that would make it a different character,
    /// in one number. The backdrop pass rebuilds the figure when this moves -
    /// rebuilding one is a texture composite, too expensive to do on the
    /// chance that something changed.
    ///
    /// A fingerprint rather than a counter the setters bump, because there are
    /// eight of them and a ninth added later would not know to bump it. This
    /// cannot be forgotten.
    [[nodiscard]] uint64_t appearanceKey() const {
        uint64_t k = static_cast<uint64_t>(raceIndex) & 0xFF;
        k = (k << 8) | (gender == game::Gender::FEMALE ? 1u : 0u);
        k = (k << 8) | skin;
        k = (k << 8) | face;
        k = (k << 8) | hairStyle;
        k = (k << 8) | hairColor;
        k = (k << 8) | facialHair;
        return k;
    }
};

/// The one selection this process has. The glue screens are a single interface
/// and there is never a second set of them.
GlueSelection& glueSelection();

}  // namespace wowee::addons
