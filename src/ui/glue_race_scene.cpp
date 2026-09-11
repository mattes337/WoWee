#include "ui/glue_race_scene.hpp"

namespace wowee::ui {

rendering::GlueSceneState glueRaceScene(game::Race race, game::Class characterClass) {
    // The model's name, as SetBackgroundModel spells it.
    const char* name = nullptr;
    switch (race) {
        case game::Race::HUMAN:     name = "Human";      break;
        case game::Race::ORC:       name = "Orc";        break;
        case game::Race::DWARF:     name = "Dwarf";      break;
        case game::Race::NIGHT_ELF: name = "NightElf";   break;
        case game::Race::UNDEAD:    name = "Scourge";    break;
        case game::Race::TAUREN:    name = "Tauren";     break;
        case game::Race::GNOME:     name = "Dwarf";      break;   // GlueParent's own hack
        case game::Race::TROLL:     name = "Orc";        break;   // and its other one
        case game::Race::BLOOD_ELF: name = "BloodElf";   break;
        case game::Race::DRAENEI:   name = "Draenei";    break;
        default: break;
    }
    // CharacterSelect_SelectCharacter puts the class file name in the race's
    // place for a death knight, so every one of them stands in Acherus.
    if (characterClass == game::Class::DEATH_KNIGHT) name = "DeathKnight";

    rendering::GlueSceneState scene;
    if (name == nullptr) return scene;
    scene.model = std::string("Interface\\Glues\\Models\\UI_") + name + "\\UI_" + name + ".mdx";
    scene.cameraIndex = 0;
    scene.sequence = 0;

    // CharModelFogInfo, keyed the way GlueParent keys it - by the model's
    // name, so a gnome gets the dwarves' fog with the dwarves' scene and a
    // death knight, with no row, gets ClearFog.
    struct FogRow { const char* race; float r, g, b, far; };
    static constexpr FogRow kFog[] = {
        {"Human",    0.8f,  0.65f, 0.73f, 222.0f},
        {"Orc",      0.5f,  0.5f,  0.5f,  270.0f},
        {"Dwarf",    0.85f, 0.88f, 1.0f,  500.0f},
        {"NightElf", 0.25f, 0.22f, 0.55f, 611.0f},
        {"Tauren",   1.0f,  0.61f, 0.42f, 153.0f},
        {"Scourge",  0.0f,  0.22f, 0.22f, 26.0f},
    };
    for (const FogRow& row : kFog) {
        if (std::string(row.race) != name) continue;
        scene.fog = true;
        scene.fogStart = 0.0f;
        scene.fogEnd = row.far;
        scene.fogColor[0] = row.r;
        scene.fogColor[1] = row.g;
        scene.fogColor[2] = row.b;
        break;
    }
    return scene;
}

} // namespace wowee::ui
