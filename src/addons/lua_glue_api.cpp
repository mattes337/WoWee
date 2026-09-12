// lua_glue_api.cpp - what the login and character screens call.
//
// GlueXML is a separate interface from FrameXML with a separate vocabulary:
// the world's globals are not published to it and its own are not published to
// the world. What is here is only what the glue screens actually call - read
// out of the installation's own AccountLogin.lua, RealmList.lua,
// CharacterSelect.lua, CharacterCreate.lua and GlueParent.lua, not guessed at.
//
// What is real here and what is not:
//
//   * The expansion level is real, read from the active profile - the glue
//     screens gate the character-creation races and the login art on it.
//   * The character list is real, off the character list the world handler
//     holds: names, races, classes, levels and zones, selecting one, entering
//     the world with it, and deleting one all reach the same calls this
//     client's own character screen makes.
//   * The realm list is real, read off the auth handler's own rows - the
//     population, the type, whether a realm is down, locked or a version
//     behind are all fields the server sent, not decorations.
//   * Logging in is real, and so is cancelling: DefaultServerLogin
//     authenticates against the server this client last used, because the
//     original client's own DefaultServerLogin carries no address either - it
//     reads one from its realmlist file. What the login is doing is real too:
//     the status dialog's Connecting, Authenticating and the line saying why
//     it stopped are the auth handler's own states, announced as the three
//     status-dialog events GlueDialog.lua registers for, and this is the
//     button on that dialog.
//   * Character creation's race, class, gender and appearance are real, and so
//     is CreateCharacter. Its *hair* half is not: the names of a race's hair
//     and facial-hair categories live in ChrRaces.dbc and nothing in this
//     layer can read a DBC, so GetHairCustomization and its sibling stay
//     unbound rather than answering with an invented category.
//   * The saved account name, the account list and the authenticator flag are
//     real, kept with wowee's own configuration rather than in the original
//     client's WTF, so launching either client does not overwrite what the
//     other saved.
//   * The background scenes are real: which frame holds one, which model it
//     holds, which of the model's cameras and animations it wants, and the fog
//     and lights GlueParent's SetLighting puts around it are all recorded here
//     and drawn by the client - see rendering::GlueScene. One thing a screen
//     can say about a scene is recorded and not drawn, and is named on
//     GlueScene::show: where in an animation to sit.
//   * The music and the ambience are real: both name a row in
//     SoundEntries.dbc and both reach the audio coordinator, which reads the
//     table and plays what the row names.
//   * The character-select camera angle is recorded and nothing more. The glue
//     screens hand the client a frame to draw a character into and an angle to
//     draw it at; that half is not here, so it is remembered and can be asked
//     for rather than being dropped on the floor.
//
// Anything a glue screen calls that is not here answers through the
// missing-API fallback, which records the name - so the gap stays visible.

#include "addons/glue_selection.hpp"
#include "addons/lua_api_helpers.hpp"
#include "addons/lua_api_registrations.hpp"
#include "audio/audio_coordinator.hpp"
#include "auth/auth_handler.hpp"
#include "core/config_paths.hpp"
#include "core/open_url.hpp"
#include "game/character.hpp"
#include "game/expansion_profile.hpp"
#include "game/world_packets.hpp"

#include <lua.h>
#include <lauxlib.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace wowee::addons {

GlueSelection& glueSelection() {
    static GlueSelection s;
    return s;
}


namespace {

// ---------------------------------------------------------------------------
// Saved login state
// ---------------------------------------------------------------------------

/// Where the login screen's remembered state is kept.
///
/// Deliberately not the original client's WTF: launching either client must
/// not overwrite what the other saved, which is the plan's own rule for saved
/// state and the reason saved variables moved here too.
std::string savedAccountPath() {
    return core::getConfigRoot() + "/glue_account.cfg";
}

/// The three things the login screen remembers between runs.
///
/// The account name is the file's first line and always has been; the other
/// two are `key=value` lines after it, so a file written before they existed
/// still reads as the name it holds.
struct SavedAccount {
    std::string name;
    /// The account picker's entries, in the original client's own format:
    /// names separated by '|', the selected one prefixed with '!'.
    /// AccountLogin.lua parses exactly that, so it is stored exactly that way.
    std::string list;
    bool usesToken = false;
    bool loaded = false;
};

SavedAccount& savedAccount() {
    static SavedAccount s;
    if (!s.loaded) {
        s.loaded = true;
        std::ifstream in(savedAccountPath());
        if (in) {
            bool first = true;
            std::string line;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                    line.pop_back();
                }
                if (first) { s.name = line; first = false; continue; }
                if (line.rfind("list=", 0) == 0)  s.list = line.substr(5);
                if (line.rfind("token=", 0) == 0) s.usesToken = (line.substr(6) == "1");
            }
        }
    }
    return s;
}

void writeSavedAccount() {
    const SavedAccount& s = savedAccount();
    std::error_code ec;
    std::filesystem::create_directories(core::getConfigRoot(), ec);
    std::ofstream out(savedAccountPath(), std::ios::trunc);
    if (!out) {
        LOG_WARNING("Glue: could not write ", savedAccountPath());
        return;
    }
    out << s.name << "\n"
        << "list=" << s.list << "\n"
        << "token=" << (s.usesToken ? 1 : 0) << "\n";
}

// ---------------------------------------------------------------------------
// Race and class art
// ---------------------------------------------------------------------------

/// The name a race goes by in the glue screens' own tables.
///
/// `token` is what RACE_ICON_TCOORDS, GlueAmbienceTracks and the RACE_INFO_*
/// strings are keyed by; `model` is the background under
/// Interface\Glues\Models\UI_<model>. They differ for two races because the
/// installation carries no model of its own for them - there is no
/// UI_Gnome and no UI_Troll - and the pairing is the installation's own:
/// GlueParent.lua names the ambience beds GlueScreenDwarfGnome and
/// GlueScreenOrcTroll.
///
/// The order is the one this client's own creation screen uses: Alliance in
/// display order, then Horde.
struct RaceArt {
    game::Race  race;
    const char* token;
    const char* model;
    bool        alliance;
};

constexpr RaceArt kRaceArt[] = {
    {game::Race::HUMAN,     "HUMAN",    "Human",    true},
    {game::Race::DWARF,     "DWARF",    "Dwarf",    true},
    {game::Race::NIGHT_ELF, "NIGHTELF", "NightElf", true},
    {game::Race::GNOME,     "GNOME",    "Dwarf",    true},
    {game::Race::DRAENEI,   "DRAENEI",  "Draenei",  true},
    {game::Race::ORC,       "ORC",      "Orc",      false},
    {game::Race::UNDEAD,    "SCOURGE",  "Scourge",  false},
    {game::Race::TAUREN,    "TAUREN",   "Tauren",   false},
    {game::Race::TROLL,     "TROLL",    "Orc",      false},
    {game::Race::BLOOD_ELF, "BLOODELF", "BloodElf", false},
};

/// CLASS_ICON_TCOORDS' keys and the CLASS_INFO_* string prefixes, in the order
/// this client's own creation screen lists them.
struct ClassArt {
    game::Class cls;
    const char* token;
};

constexpr ClassArt kClassArt[] = {
    {game::Class::WARRIOR,      "WARRIOR"},
    {game::Class::PALADIN,      "PALADIN"},
    {game::Class::HUNTER,       "HUNTER"},
    {game::Class::ROGUE,        "ROGUE"},
    {game::Class::PRIEST,       "PRIEST"},
    {game::Class::DEATH_KNIGHT, "DEATHKNIGHT"},
    {game::Class::SHAMAN,       "SHAMAN"},
    {game::Class::MAGE,         "MAGE"},
    {game::Class::WARLOCK,      "WARLOCK"},
    {game::Class::DRUID,        "DRUID"},
};

const RaceArt* raceArtFor(game::Race race) {
    for (const auto& a : kRaceArt) if (a.race == race) return &a;
    return nullptr;
}

const ClassArt* classArtFor(game::Class cls) {
    for (const auto& a : kClassArt) if (a.cls == cls) return &a;
    return nullptr;
}

/// The background the character screen shows behind a character, which is the
/// one for its race - except for a death knight, who gets their own whatever
/// they were born as. That exception is the installation's own: it carries a
/// UI_DeathKnight model and GlueAmbienceTracks has a DEATHKNIGHT bed, and
/// CharacterSelect.lua's own CharacterSelect_DeathKnightSwap tests for exactly
/// that name.
const char* backgroundModelFor(game::Race race, game::Class cls) {
    if (cls == game::Class::DEATH_KNIGHT) return "DeathKnight";
    const RaceArt* art = raceArtFor(race);
    return art ? art->model : "Human";
}

// ---------------------------------------------------------------------------
// What the glue screens have chosen
// ---------------------------------------------------------------------------

/// The selection the glue screens are holding - see addons/glue_selection.hpp,
/// where it moved to when the backdrop pass started reading it to know which
/// figure to stand in the scene.
GlueSelection& selection() { return glueSelection(); }

/// The races character creation offers, in the order the buttons are laid out.
///
/// The active profile's list when it has one - a classic realm must not offer
/// the two Burning Crusade races - and everything this client knows when it
/// does not.
std::vector<game::Race> availableRaces(lua_State* L) {
    std::vector<game::Race> out;
    auto* svc = getLuaServices(L);
    auto* reg = svc ? svc->expansionRegistry : nullptr;
    const auto* profile = reg ? reg->getActive() : nullptr;
    for (const auto& art : kRaceArt) {
        if (profile && !profile->races.empty()) {
            const auto id = static_cast<uint32_t>(art.race);
            if (std::find(profile->races.begin(), profile->races.end(), id) ==
                profile->races.end()) {
                continue;
            }
        }
        out.push_back(art.race);
    }
    return out;
}

std::vector<game::Class> availableClasses(lua_State* L) {
    std::vector<game::Class> out;
    auto* svc = getLuaServices(L);
    auto* reg = svc ? svc->expansionRegistry : nullptr;
    const auto* profile = reg ? reg->getActive() : nullptr;
    for (const auto& art : kClassArt) {
        if (profile && !profile->classes.empty()) {
            const auto id = static_cast<uint32_t>(art.cls);
            if (std::find(profile->classes.begin(), profile->classes.end(), id) ==
                profile->classes.end()) {
                continue;
            }
        }
        out.push_back(art.cls);
    }
    return out;
}

game::Race selectedRace(lua_State* L) {
    const auto races = availableRaces(L);
    if (races.empty()) return game::Race::HUMAN;
    int i = selection().raceIndex - 1;
    if (i < 0 || i >= static_cast<int>(races.size())) i = 0;
    // Remembered on the way out. Resolving the row needs this list, the list
    // needs the expansion profile off the Lua services, and the pass that
    // draws the figure has neither - so the answer is written where it can
    // read it rather than computed a second way. Every handler that cares
    // about the race calls this, so it does not go stale.
    selection().race = races[static_cast<size_t>(i)];
    return selection().race;
}

game::Class selectedClass(lua_State* L) {
    const auto classes = availableClasses(L);
    if (classes.empty()) return game::Class::WARRIOR;
    int i = selection().classIndex - 1;
    if (i < 0 || i >= static_cast<int>(classes.size())) i = 0;
    return classes[static_cast<size_t>(i)];
}

/// Move the class selection onto one this race may actually be, which is what
/// the original screen expects after a race button is pressed: it reads the
/// selected class back and puts its name on the panel.
void clampClassToRace(lua_State* L) {
    const auto classes = availableClasses(L);
    const game::Race race = selectedRace(L);
    if (classes.empty()) return;
    const int current = selection().classIndex - 1;
    if (current >= 0 && current < static_cast<int>(classes.size()) &&
        game::isValidRaceClassCombo(race, classes[static_cast<size_t>(current)])) {
        return;
    }
    for (size_t i = 0; i < classes.size(); ++i) {
        if (game::isValidRaceClassCombo(race, classes[i])) {
            selection().classIndex = static_cast<int>(i) + 1;
            return;
        }
    }
}

/// The highest value each of the five customisation dials accepts, in the
/// order the original screen numbers them: 1 skin, 2 face, 3 hair style,
/// 4 hair colour, 5 facial hair. Those are the CHAR_CUSTOMIZATION<N>_DESC
/// rows, and CharacterCreate.lua overwrites the labels of 3, 4 and 5 from the
/// race's own hair categories.
uint8_t customizationMax(int which, game::Race race, game::Gender gender) {
    switch (which) {
        case 1: return game::getMaxSkin(race, gender);
        case 2: return game::getMaxFace(race, gender);
        case 3: return game::getMaxHairStyle(race, gender);
        case 4: return game::getMaxHairColor(race, gender);
        case 5: return game::getMaxFacialFeature(race, gender);
        default: return 0;
    }
}

uint8_t* customizationValue(int which) {
    GlueSelection& s = selection();
    switch (which) {
        case 1: return &s.skin;
        case 2: return &s.face;
        case 3: return &s.hairStyle;
        case 4: return &s.hairColor;
        case 5: return &s.facialHair;
        default: return nullptr;
    }
}

void randomizeCustomization(lua_State* L) {
    static std::mt19937 rng{std::random_device{}()};
    const game::Race race = selectedRace(L);
    const game::Gender gender = selection().gender;
    for (int which = 1; which <= 5; ++which) {
        uint8_t* value = customizationValue(which);
        if (!value) continue;
        const uint8_t max = customizationMax(which, race, gender);
        *value = static_cast<uint8_t>(
            std::uniform_int_distribution<int>(0, max)(rng));
    }
}

/// Every value back inside what this race and gender allow.
///
/// A race change can leave a hair style the new race does not have, and the
/// server refuses the whole character for it.
void clampCustomization(lua_State* L) {
    const game::Race race = selectedRace(L);
    const game::Gender gender = selection().gender;
    for (int which = 1; which <= 5; ++which) {
        uint8_t* value = customizationValue(which);
        if (!value) continue;
        const uint8_t max = customizationMax(which, race, gender);
        if (*value > max) *value = max;
    }
}

// ---------------------------------------------------------------------------
// Login screen
// ---------------------------------------------------------------------------

/// The same number the world interface's GetAccountExpansionLevel answers, and
/// for the same reason: CharacterCreate reads it to decide whether the death
/// knight and the two TBC races are offered.
int lua_GetClientExpansionLevel(lua_State* L) {
    lua_pushnumber(L, expansionLevelZeroBased(L));
    return 1;
}

int lua_GetNumCharacters(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? static_cast<double>(gh->getCharacters().size()) : 0.0);
    return 1;
}

/// The account name the login box is filled in with.
///
/// The empty string rather than nil when there is none, which is what the
/// original client answers and what AccountLogin.lua tests for: it compares
/// the result against "" to decide whether to put the caret in the account box
/// or in the password box, and nil fails that comparison - so a fresh install
/// opened with the caret waiting in a password box for an account nobody had
/// typed yet.
int lua_GetSavedAccountName(lua_State* L) {
    lua_pushstring(L, savedAccount().name.c_str());
    return 1;
}

int lua_SetSavedAccountName(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    savedAccount().name = name ? name : "";
    writeSavedAccount();
    return 0;
}

/// The account picker's entries.
///
/// A string rather than nil even when there are none: AccountLogin.lua runs
/// string.gmatch over whatever this answers, and the missing-API fallback's
/// stand-in is not a string - the login screen died on that line before this
/// existed.
int lua_GetSavedAccountList(lua_State* L) {
    lua_pushstring(L, savedAccount().list.c_str());
    return 1;
}

int lua_SetSavedAccountList(lua_State* L) {
    const char* list = lua_tostring(L, 1);
    savedAccount().list = list ? list : "";
    writeSavedAccount();
    return 0;
}

/// Whether this account was last logged in to with an authenticator.
///
/// Saved state, exactly as it is in the original client: the token box is
/// shown for an account that used one last time. Whether the *server* wants a
/// code is a different question, and it is answered by the server asking.
int lua_GetUsesToken(lua_State* L) {
    lua_pushboolean(L, savedAccount().usesToken ? 1 : 0);
    return 1;
}

int lua_SetUsesToken(lua_State* L) {
    savedAccount().usesToken = lua_toboolean(L, 1) != 0;
    writeSavedAccount();
    return 0;
}

/// No trial account and no streaming install: this client installs nothing and
/// has no account tier to report. Answered rather than left to the fallback,
/// because the fallback's truthy stand-in would put the login screen into the
/// trial-conversion flow.
int lua_False(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

/// Nothing outstanding for the player to agree to, and no scan waiting to
/// finish.
///
/// This client presents no agreement: it is not Blizzard's client, there is no
/// binding that supplies the text of one, and the scan the original runs
/// before it will log in is a Windows DLL this does not load. So there is
/// nothing pending - which is what these answer, rather than claiming the
/// player signed anything.
///
/// Answered rather than left to the fallback for the same reason
/// IsTrialAccount is: the fallback's stand-in answers nil, AccountLogin.lua
/// reads that as "not yet agreed", and the login screen hides itself behind an
/// agreement frame with no text in it and no way back out.
int lua_NothingPending(lua_State* L) {
    lua_pushboolean(L, 1);
    return 1;
}

int lua_DefaultServerLogin(lua_State* L) {
    const char* account = luaL_optstring(L, 1, "");
    const char* password = luaL_optstring(L, 2, "");
    auto* svc = getLuaServices(L);
    if (!svc || !svc->glueLogin) return 0;
    svc->glueLogin(account ? account : "", password ? password : "");
    return 0;
}

int lua_CancelLogin(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->glueCancelLogin) svc->glueCancelLogin();
    return 0;
}

/// The button on the status dialog.
///
/// GlueDialog.lua's own types call it from every button that dismisses one of
/// the dialogs a login puts up - "CANCEL" while the login is still running,
/// "OKAY" and "OKAY_HTML" once it has stopped - and "DISCONNECTED" calls it
/// from OnShow. In every one of those the dialog is about a login, and the
/// only thing its button can mean is "stop": abandon the one in progress, or
/// clear away what is left of the one that failed. That is CancelLogin's job
/// exactly, so it is CancelLogin's code - with nothing in progress it
/// disconnects an already-disconnected handler and lands on the screen the
/// client is already on.
int lua_StatusDialogClick(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->glueCancelLogin) svc->glueCancelLogin();
    return 0;
}

int lua_QuitGame(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->quitApplication) svc->quitApplication();
    return 0;
}

/// Which glue screen the client should now consider itself on.
///
/// GlueParent's SetGlueScreen shows one frame, hides the rest and then says
/// which it settled on. The client's own state follows from that: it decides
/// which handler is updated each frame, so a login that never moves it leaves
/// the realm list unasked for.
int lua_SetCurrentScreen(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "");
    auto* svc = getLuaServices(L);
    if (svc && svc->setCurrentScreen) svc->setCurrentScreen(name ? name : "");
    return 0;
}

// ---------------------------------------------------------------------------
// Realm list
// ---------------------------------------------------------------------------

const std::vector<auth::Realm>* realmList(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* ah = svc ? svc->authHandler : nullptr;
    return ah ? &ah->getRealms() : nullptr;
}

/// The realm the player is on, or an empty name when none has been chosen.
LuaServices::GlueRealm currentRealm(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->getCurrentRealm) return svc->getCurrentRealm();
    return {};
}

/// Realm names, and what kind of realm each is.
///
/// GetServerName is asked three separate questions at once: the login screen
/// wants a name to put under the logo, the character screen wants the type
/// beside it, and RealmList's cancel wants to know whether the realm is down.
/// nil for all four before a realm has been picked, which is what the login
/// screen reads on a fresh run.
int lua_GetServerName(lua_State* L) {
    const auto realm = currentRealm(L);
    if (realm.name.empty()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, realm.name.c_str());
    lua_pushboolean(L, realm.pvp ? 1 : 0);
    lua_pushboolean(L, realm.rp ? 1 : 0);
    lua_pushboolean(L, realm.down ? 1 : 0);
    return 4;
}

/// The realm-list categories, which for this client is always exactly one.
///
/// The 3.3.5a realm-list packet carries no grouping: every realm the server
/// sent is in one list. RealmList.lua hides the tab strip entirely when there
/// is one category, so the name it is given is never drawn - it is the
/// installation's locale because that is what a category is in the protocol
/// this stands in for.
int lua_GetRealmCategories(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* reg = svc ? svc->expansionRegistry : nullptr;
    const auto* profile = reg ? reg->getActive() : nullptr;
    lua_pushstring(L, (profile && !profile->locale.empty()) ? profile->locale.c_str()
                                                            : "enUS");
    return 1;
}

/// Which of them is showing. One, because there is only one.
int lua_GetSelectedCategory(lua_State* L) {
    lua_pushnumber(L, 1);
    return 1;
}

int lua_GetNumRealms(lua_State* L) {
    const auto* realms = realmList(L);
    lua_pushnumber(L, realms ? static_cast<double>(realms->size()) : 0.0);
    return 1;
}

/// One row of the realm list, in the fourteen values RealmList.lua unpacks.
///
/// Every one of them is a field the server sent. The flag bits are the same
/// ones this client's own realm screen reads: 0x01 a build this client cannot
/// speak to, 0x02 offline, 0x20 recommended, 0x40 new, 0x80 full. `load` is
/// the population the realm reported, moved so that the middle of the scale is
/// zero - which is the comparison RealmList.lua makes against it.
///
/// The last five are only sent by a realm that named its build, and the Lua
/// tests for that by asking whether `major` came back at all.
int lua_GetRealmInfo(lua_State* L) {
    const auto* realms = realmList(L);
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    if (!realms || index < 1 || index > static_cast<int>(realms->size())) {
        lua_pushnil(L);
        return 1;
    }
    const auth::Realm& r = (*realms)[static_cast<size_t>(index - 1)];
    const auto currentName = currentRealm(L).name;

    float load = r.population - 1.0f;
    if (r.flags & 0x20)      load = -3.0f;   // REALM_FLAG_RECOMMENDED
    else if (r.flags & 0x40) load = -2.0f;   // REALM_FLAG_NEW
    else if (r.flags & 0x80) load =  2.0f;   // REALM_FLAG_FULL

    lua_pushstring(L, r.name.c_str());                       // name
    lua_pushnumber(L, r.characters);                         // numCharacters
    lua_pushboolean(L, (r.flags & 0x01) != 0);               // invalidRealm
    lua_pushboolean(L, (r.flags & 0x02) != 0);               // realmDown
    lua_pushnumber(L, (!currentName.empty() && currentName == r.name) ? 1 : 0);
    lua_pushboolean(L, r.icon == 1 || r.icon == 8);          // pvp
    lua_pushboolean(L, r.icon == 6 || r.icon == 8);          // rp
    lua_pushnumber(L, load);
    lua_pushboolean(L, r.lock != 0);                         // locked
    if (!r.hasVersionInfo()) return 9;
    lua_pushnumber(L, r.majorVersion);
    lua_pushnumber(L, r.minorVersion);
    lua_pushnumber(L, r.patchVersion);
    lua_pushnumber(L, r.build);
    lua_pushnumber(L, r.icon);                               // type
    return 14;
}

/// How long the realm list is shown before it is asked for again, in seconds.
///
/// The original client reads this out of its own configuration; this is a
/// number rather than a fact about the server, and it is this client's. Long
/// enough that a player reading the list is not fighting a redraw, short
/// enough that a realm coming back up shows within a few seconds.
int lua_RealmListUpdateRate(lua_State* L) {
    lua_pushnumber(L, 15);
    return 1;
}

int lua_RequestRealmList(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* ah = svc ? svc->authHandler : nullptr;
    if (ah) ah->requestRealmList();
    return 0;
}

int lua_ChangeRealm(lua_State* L) {
    // (category, realmIndex) - the category is always one here, so only the
    // row matters.
    const int index = static_cast<int>(luaL_optnumber(L, 2, 0));
    auto* svc = getLuaServices(L);
    if (svc && svc->glueChangeRealm) svc->glueChangeRealm(index);
    return 0;
}

/// No realm this client can see is restricted by locale or reserved for a
/// tournament: the 3.3.5a realm list has no field that says either, so the
/// answer is no rather than unknown. Answered rather than left to the fallback
/// because its truthy stand-in would put a warning dialog over every realm
/// the player clicked.
int lua_FalseCategoryTest(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

// ---------------------------------------------------------------------------
// Character select
// ---------------------------------------------------------------------------

const game::Character* characterAt(lua_State* L, int index) {
    auto* gh = getGameHandler(L);
    if (!gh) return nullptr;
    const auto& chars = gh->getCharacters();
    if (index < 1 || index > static_cast<int>(chars.size())) return nullptr;
    return &chars[static_cast<size_t>(index - 1)];
}

/// A character's row on the character screen.
///
/// The tenth, eleventh and twelfth values are the pending paid customisation,
/// race change and faction change; this client sells none of those, so they
/// stay nil and the three little icons the screen would draw for them stay
/// hidden.
int lua_GetCharacterInfo(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const game::Character* ch = characterAt(L, index);
    if (!ch) {
        lua_pushnil(L);
        return 1;
    }
    auto* gh = getGameHandler(L);
    lua_pushstring(L, ch->name.c_str());
    lua_pushstring(L, game::getRaceName(ch->race));
    lua_pushstring(L, game::getClassName(ch->characterClass));
    lua_pushnumber(L, ch->level);
    const std::string zone = gh ? gh->getWhoAreaName(ch->zoneId) : std::string{};
    lua_pushstring(L, zone.c_str());
    // The glue screens count gender from SEX_MALE = 2, which is what
    // GetSelectedSex answers with and what the gender buttons compare against.
    lua_pushnumber(L, ch->gender == game::Gender::FEMALE ? 3 : 2);
    // CHARACTER_FLAG_GHOST, the same bit the server sets in the character
    // enumeration it sent - see AzerothCore/TrinityCore CharacterFlags.
    lua_pushboolean(L, (ch->flags & 0x00002000u) != 0);
    return 7;
}

/// Which row is highlighted.
///
/// Sets the character on the world handler rather than logging in with it:
/// the original screen selects on a single click and enters the world on a
/// second button, exactly as this client's own screen does.
int lua_SelectCharacter(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const game::Character* ch = characterAt(L, index);
    auto* gh = getGameHandler(L);
    if (!ch || !gh) return 0;
    selection().characterIndex = index;
    gh->setActiveCharacterGuid(ch->guid);
    return 0;
}

/// Enter the world with the highlighted character.
///
/// The same two calls this client's own character screen makes, in the same
/// order: the guid is set so everything downstream knows who logged in, and
/// the login is sent.
int lua_EnterWorld(lua_State* L) {
    auto* gh = getGameHandler(L);
    const game::Character* ch = characterAt(L, selection().characterIndex);
    if (!gh || !ch) return 0;
    gh->setActiveCharacterGuid(ch->guid);
    gh->selectCharacter(ch->guid);
    return 0;
}

int lua_DeleteCharacter(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const game::Character* ch = characterAt(L, index);
    auto* gh = getGameHandler(L);
    if (ch && gh) gh->deleteCharacter(ch->guid);
    return 0;
}

int lua_GetCharacterListUpdate(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) gh->requestCharacterList();
    return 0;
}

int lua_DisconnectFromServer(lua_State* L) {
    auto* gh = getGameHandler(L);
    if (gh) gh->disconnect();
    return 0;
}

/// The background behind a character on the character screen.
///
/// A name rather than a path: SetBackgroundModel builds
/// Interface\Glues\Models\UI_<name>\UI_<name>.m2 from it and looks the
/// ambience and the lighting up under its upper case.
int lua_GetSelectBackgroundModel(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const game::Character* ch = characterAt(L, index);
    if (!ch) {
        lua_pushstring(L, "MainMenu");
        return 1;
    }
    lua_pushstring(L, backgroundModelFor(ch->race, ch->characterClass));
    return 1;
}

int lua_GetCreateBackgroundModel(lua_State* L) {
    lua_pushstring(L, backgroundModelFor(selectedRace(L), selectedClass(L)));
    return 1;
}

// ---------------------------------------------------------------------------
// Character creation
// ---------------------------------------------------------------------------

/// The race buttons: a display name, an art token and whether the race may be
/// chosen, three values per race.
///
/// Every race in the list is one the active profile allows, so all of them are
/// enabled - a race the expansion does not have is absent rather than greyed.
int lua_GetAvailableRaces(lua_State* L) {
    const auto races = availableRaces(L);
    for (const auto race : races) {
        const RaceArt* art = raceArtFor(race);
        lua_pushstring(L, game::getRaceName(race));
        lua_pushstring(L, art ? art->token : "HUMAN");
        lua_pushnumber(L, 1);
    }
    return static_cast<int>(races.size()) * 3;
}

/// The class buttons, the same three values per class.
///
/// Enabled here means the expansion has the class at all; whether *this* race
/// may be it is asked separately, by IsRaceClassValid, which is what the
/// original screen does with the answer.
int lua_GetAvailableClasses(lua_State* L) {
    const auto classes = availableClasses(L);
    for (const auto cls : classes) {
        const ClassArt* art = classArtFor(cls);
        lua_pushstring(L, game::getClassName(cls));
        lua_pushstring(L, art ? art->token : "WARRIOR");
        lua_pushnumber(L, 1);
    }
    return static_cast<int>(classes.size()) * 3;
}

int lua_GetSelectedRace(lua_State* L) {
    lua_pushnumber(L, selection().raceIndex);
    return 1;
}

int lua_SetSelectedRace(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 1));
    const auto races = availableRaces(L);
    if (index < 1 || index > static_cast<int>(races.size())) return 0;
    selection().raceIndex = index;
    clampClassToRace(L);
    clampCustomization(L);
    return 0;
}

/// The class, its art token, and which button it is.
///
/// Three values where the original client answers six: the last three are the
/// tank, healer and damage roles, and nothing in this client knows them - they
/// are not in the class list the server sends and not in any table here.
/// CharacterCreate.lua reads only the first three.
int lua_GetSelectedClass(lua_State* L) {
    const game::Class cls = selectedClass(L);
    const ClassArt* art = classArtFor(cls);
    lua_pushstring(L, game::getClassName(cls));
    lua_pushstring(L, art ? art->token : "WARRIOR");
    lua_pushnumber(L, selection().classIndex);
    return 3;
}

int lua_SetSelectedClass(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 1));
    const auto classes = availableClasses(L);
    if (index < 1 || index > static_cast<int>(classes.size())) return 0;
    if (!game::isValidRaceClassCombo(selectedRace(L),
                                     classes[static_cast<size_t>(index - 1)])) {
        return 0;
    }
    selection().classIndex = index;
    return 0;
}

int lua_GetSelectedSex(lua_State* L) {
    lua_pushnumber(L, selection().gender == game::Gender::FEMALE ? 3 : 2);
    return 1;
}

int lua_SetSelectedSex(lua_State* L) {
    const int sex = static_cast<int>(luaL_optnumber(L, 1, 2));
    selection().gender = (sex == 3) ? game::Gender::FEMALE : game::Gender::MALE;
    clampCustomization(L);
    return 0;
}

int lua_IsRaceClassValid(lua_State* L) {
    const int raceIndex = static_cast<int>(luaL_optnumber(L, 1, 0));
    const int classIndex = static_cast<int>(luaL_optnumber(L, 2, 0));
    const auto races = availableRaces(L);
    const auto classes = availableClasses(L);
    if (raceIndex < 1 || raceIndex > static_cast<int>(races.size()) ||
        classIndex < 1 || classIndex > static_cast<int>(classes.size())) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, game::isValidRaceClassCombo(
        races[static_cast<size_t>(raceIndex - 1)],
        classes[static_cast<size_t>(classIndex - 1)]) ? 1 : 0);
    return 1;
}

/// The faction a race belongs to, twice: once as a name to show and once as
/// the key FACTION_BACKDROP_COLOR_TABLE is written with. They are the same
/// word in English, and this client has no localised faction names to draw the
/// two apart.
int lua_GetFactionForRace(lua_State* L) {
    const int index = static_cast<int>(luaL_optnumber(L, 1, 0));
    const auto races = availableRaces(L);
    bool alliance = true;
    if (index >= 1 && index <= static_cast<int>(races.size())) {
        const RaceArt* art = raceArtFor(races[static_cast<size_t>(index - 1)]);
        if (art) alliance = art->alliance;
    }
    lua_pushstring(L, alliance ? "Alliance" : "Horde");
    lua_pushstring(L, alliance ? "Alliance" : "Horde");
    return 2;
}

/// The selected race's display name and its art token, which the screen builds
/// RACE_INFO_* and the portrait's texture coordinates from.
int lua_GetNameForRace(lua_State* L) {
    const game::Race race = selectedRace(L);
    const RaceArt* art = raceArtFor(race);
    lua_pushstring(L, game::getRaceName(race));
    lua_pushstring(L, art ? art->token : "HUMAN");
    return 2;
}

int lua_CycleCharCustomization(lua_State* L) {
    const int which = static_cast<int>(luaL_optnumber(L, 1, 0));
    const int direction = static_cast<int>(luaL_optnumber(L, 2, 1));
    uint8_t* value = customizationValue(which);
    if (!value) return 0;
    const int max = customizationMax(which, selectedRace(L), selection().gender);
    int next = static_cast<int>(*value) + (direction < 0 ? -1 : 1);
    // The original screen's arrows wrap, and the player has no other way back
    // past either end.
    if (next < 0) next = max;
    if (next > max) next = 0;
    *value = static_cast<uint8_t>(next);
    return 0;
}

int lua_RandomizeCharCustomization(lua_State* L) {
    randomizeCustomization(L);
    return 0;
}

/// A fresh character to start from, which the original screen asks for every
/// time it is shown - and which it describes as a random combination.
int lua_ResetCharCustomize(lua_State* L) {
    selection().raceIndex = 1;
    selection().classIndex = 1;
    clampClassToRace(L);
    randomizeCustomization(L);
    return 0;
}

int lua_CreateCharacter(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "");
    auto* gh = getGameHandler(L);
    if (!gh || !name || !*name) return 0;
    game::CharCreateData data;
    data.name = name;
    data.race = selectedRace(L);
    data.characterClass = selectedClass(L);
    data.gender = selection().gender;
    data.skin = selection().skin;
    data.face = selection().face;
    data.hairStyle = selection().hairStyle;
    data.hairColor = selection().hairColor;
    data.facialHair = selection().facialHair;
    gh->createCharacter(data);
    return 0;
}

// ---------------------------------------------------------------------------
// What the renderer has not caught up with
// ---------------------------------------------------------------------------

/// The frame the glue screens want a character drawn into, and the background
/// they want behind it.
///
/// Recorded under a name the client can read back: the glue screens set both
/// once and never ask again, so a no-op would lose which frame was asked for.
int rememberString(lua_State* L, const char* key) {
    lua_pushvalue(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, key);
    return 0;
}

std::string rememberedString(lua_State* L, const char* key) {
    lua_getfield(L, LUA_REGISTRYINDEX, key);
    const char* s = lua_tostring(L, -1);
    std::string out = s ? s : "";
    lua_pop(L, 1);
    return out;
}

/// What each glue model frame was told about its scene, by the frame's name.
///
/// One table rather than a field per screen, because the screens do not agree
/// on how they say it: CharacterSelect.lua and CharacterCreate.lua name their
/// frame first (SetCharSelectModelFrame, SetCharCustomizeFrame) and then hand
/// over a path through SetBackgroundModel, while AccountLogin.lua sets its own
/// model on itself. What the client needs out of all three is the same thing -
/// this frame, that scene - so that is what is kept, and the drawing half can
/// ask about whichever frame is on screen without knowing which route said so.
///
/// Each entry is a table: `path`, and then whatever of `camera`, `sequence`,
/// `sequenceTime`, `scale`, `fog`, `fogStart`, `fogEnd`, `fogR/G/B`, `glow`
/// and the three light lists the screen has said so far. Read back by
/// Application::updateGlueBackdrop, which is the only other place that knows
/// these names.
constexpr const char* kGlueScenes = "wowee_glue_scenes";

/// Pushes frame `frameName`'s entry, creating it and the outer table if they
/// are not there yet. Leaves exactly one value on the stack, and false with
/// nothing on it for a frame with no name to file it under.
bool pushSceneEntry(lua_State* L, const std::string& frameName) {
    if (frameName.empty()) return false;
    lua_getfield(L, LUA_REGISTRYINDEX, kGlueScenes);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, kGlueScenes);
    }
    lua_getfield(L, -1, frameName.c_str());
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, frameName.c_str());
    }
    lua_remove(L, -2);              // the outer table; the entry is what is wanted
    return true;
}

void recordModelPath(lua_State* L, const std::string& frameName, const char* path) {
    if (frameName.empty() || !path || !*path) return;
    if (!pushSceneEntry(L, frameName)) return;
    lua_pushstring(L, path);
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}

/// The name of the frame a widget method was called on. Empty for an
/// anonymous frame, which nothing here can file anything under.
std::string modelFrameName(lua_State* L) {
    if (!lua_istable(L, 1)) return {};
    lua_pushstring(L, "__name");
    lua_rawget(L, 1);
    const char* name = lua_tostring(L, -1);
    std::string out = name ? name : "";
    lua_pop(L, 1);
    return out;
}

void recordSceneNumber(lua_State* L, const char* key, double value) {
    const std::string frame = modelFrameName(L);
    if (!pushSceneEntry(L, frame)) return;
    lua_pushnumber(L, value);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
}

void recordSceneBool(lua_State* L, const char* key, bool value) {
    const std::string frame = modelFrameName(L);
    if (!pushSceneEntry(L, frame)) return;
    lua_pushboolean(L, value ? 1 : 0);
    lua_setfield(L, -2, key);
    lua_pop(L, 1);
}

int lua_SetCharSelectModelFrame(lua_State* L) {
    return rememberString(L, "wowee_charselect_model_frame");
}

int lua_SetCharCustomizeFrame(lua_State* L) {
    return rememberString(L, "wowee_charcustomize_model_frame");
}

int lua_SetCharSelectBackground(lua_State* L) {
    rememberString(L, "wowee_charselect_background");
    recordModelPath(L, rememberedString(L, "wowee_charselect_model_frame"),
                    lua_tostring(L, 1));
    return 0;
}

int lua_SetCharCustomizeBackground(lua_State* L) {
    rememberString(L, "wowee_charcustomize_background");
    recordModelPath(L, rememberedString(L, "wowee_charcustomize_model_frame"),
                    lua_tostring(L, 1));
    return 0;
}

/// Model:SetModel(path), for the one glue screen that never announces its
/// scene any other way.
///
/// AccountLogin_OnLoad sets the login backdrop by calling this on itself -
/// Interface\Glues\Models\UI_MainMenu_Northrend\UI_MainMenu_Northrend.m2 -
/// rather than going through SetBackgroundModel, so without it the login
/// screen's model is the one thing the glue vocabulary never says out loud.
/// Recorded under the frame's own name, beside the two the character screens
/// do announce.
///
/// Not registered as a widget method here: the frame metatable does not exist
/// yet when the glue API is registered. The client installs it, and only for a
/// run that is loading GlueXML - see Application::initialize.
int lua_GlueSetModelPath(lua_State* L) {
    if (!lua_istable(L, 1)) return 0;
    const char* path = lua_tostring(L, 2);
    if (!path || !*path) return 0;
    recordModelPath(L, modelFrameName(L), path);
    return 0;
}

// --- The rest of what a model frame is told -------------------------------
//
// Recorded rather than acted on here, for the same reason SetModel is: this
// layer cannot reach a renderer, and the frame these are called on is the one
// the client draws the scene into. Application::updateGlueBackdrop reads the
// entry back for whichever glue frame is on screen and hands it to
// rendering::GlueScene, which is where the fog, the lights and the camera
// actually take effect.
//
// All of them were no-ops before, and a no-op is invisible: AccountLogin.xml's
// fogNear="0" fogFar="1200" glow="0.08" and GlueParent's whole SetLighting -
// every light behind every glue screen - reached this client and stopped.

int lua_GlueSetCamera(lua_State* L) {
    recordSceneNumber(L, "camera", luaL_optnumber(L, 2, 0));
    return 0;
}

int lua_GlueSetSequence(lua_State* L) {
    recordSceneNumber(L, "sequence", luaL_optnumber(L, 2, 0));
    return 0;
}

/// SetSequenceTime(sequence, milliseconds): which animation, and where in it.
int lua_GlueSetSequenceTime(lua_State* L) {
    recordSceneNumber(L, "sequence", luaL_optnumber(L, 2, 0));
    recordSceneNumber(L, "sequenceTime", luaL_optnumber(L, 3, 0));
    return 0;
}

int lua_GlueSetModelScale(lua_State* L) {
    recordSceneNumber(L, "scale", luaL_optnumber(L, 2, 1.0));
    return 0;
}

int lua_GlueSetGlow(lua_State* L) {
    recordSceneNumber(L, "glow", luaL_optnumber(L, 2, 0));
    return 0;
}

/// Any of the three fog setters turns fog on, and ClearFog turns it off.
///
/// There is no separate switch in the interface's vocabulary: a screen that
/// wants fog says where it starts and ends, and one that does not calls
/// ClearFog. AccountLogin.xml names a range and no colour, so the colour has
/// to default to something - black, which is what the original leaves it as
/// and what a scene fades into on the login screen.
int lua_GlueSetFogNear(lua_State* L) {
    recordSceneBool(L, "fog", true);
    recordSceneNumber(L, "fogStart", luaL_optnumber(L, 2, 0));
    return 0;
}

int lua_GlueSetFogFar(lua_State* L) {
    recordSceneBool(L, "fog", true);
    recordSceneNumber(L, "fogEnd", luaL_optnumber(L, 2, 0));
    return 0;
}

int lua_GlueSetFogColor(lua_State* L) {
    recordSceneBool(L, "fog", true);
    recordSceneNumber(L, "fogR", luaL_optnumber(L, 2, 0));
    recordSceneNumber(L, "fogG", luaL_optnumber(L, 3, 0));
    recordSceneNumber(L, "fogB", luaL_optnumber(L, 4, 0));
    return 0;
}

int lua_GlueClearFog(lua_State* L) {
    recordSceneBool(L, "fog", false);
    return 0;
}

/// The three light lists a frame carries, emptied.
///
/// GlueParent's own comment is the specification: ResetLights puts all six
/// light sets back to the background's defaults, and adding a light to any one
/// set replaces every default in that set. This client does not read a model's
/// own lights, so "back to default" means "back to the rig the client lights
/// an unlit glue scene with" - which is what an empty list is read as.
int lua_GlueResetLights(lua_State* L) {
    const std::string frame = modelFrameName(L);
    if (!pushSceneEntry(L, frame)) return 0;
    for (const char* key : {"lights", "characterLights", "petLights"}) {
        lua_newtable(L);
        lua_setfield(L, -2, key);
    }
    lua_pop(L, 1);
    return 0;
}

/// AddLight(set, enabled, type, dirX, dirY, dirZ, ambIntensity, ambR, ambG,
///          ambB, difIntensity, difR, difG, difB)
///
/// Fourteen numbers after the frame: the light set, and then the thirteen a
/// RaceLights row holds, which GlueParent unpacks straight into the call.
/// Stored as they arrive, because the reading of them belongs with the
/// renderer that has to merge them and not with the recording.
///
/// A light the row marks disabled is dropped here rather than carried: the
/// interface's own loop already skips those, so one arriving means a caller
/// that did not, and a disabled light is not a light.
int lua_GlueAddLightTo(lua_State* L, const char* key) {
    const std::string frame = modelFrameName(L);
    if (!pushSceneEntry(L, frame)) return 0;
    if (luaL_optnumber(L, 3, 1) == 0) { lua_pop(L, 1); return 0; }   // not enabled

    lua_getfield(L, -1, key);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, key);
    }
    lua_newtable(L);
    for (int arg = 2; arg <= 15; ++arg) {
        lua_pushnumber(L, luaL_optnumber(L, arg, 0));
        lua_rawseti(L, -2, arg - 1);
    }
    // Four per set is the ceiling the interface documents. Past it the extra
    // ones are dropped rather than growing the list without limit, because a
    // screen that keeps adding without resetting is the shape of a leak.
    const lua_Integer count = static_cast<lua_Integer>(lua_objlen(L, -2));
    if (count >= 4) {
        lua_pop(L, 2);
    } else {
        lua_rawseti(L, -2, count + 1);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return 0;
}

int lua_GlueAddLight(lua_State* L)          { return lua_GlueAddLightTo(L, "lights"); }
int lua_GlueAddCharacterLight(lua_State* L) { return lua_GlueAddLightTo(L, "characterLights"); }
int lua_GlueAddPetLight(lua_State* L)       { return lua_GlueAddLightTo(L, "petLights"); }

/// Put the model-frame methods on the frame metatable.
///
/// Called by the client immediately before it loads GlueXML, and not from
/// registerGlueLuaAPI below, because the metatable does not exist yet when
/// this file's globals are registered - the engine builds it afterwards. Only
/// a run that is loading the glue screens does this; with the world's
/// interface up these stay exactly the no-ops they were.
int lua_GlueInstallModelMethods(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction func;
    } methods[] = {
        {"SetModel",           lua_GlueSetModelPath},
        {"SetCamera",          lua_GlueSetCamera},
        {"SetSequence",        lua_GlueSetSequence},
        {"SetSequenceTime",    lua_GlueSetSequenceTime},
        {"SetModelScale",      lua_GlueSetModelScale},
        {"SetGlow",            lua_GlueSetGlow},
        {"SetFogNear",         lua_GlueSetFogNear},
        {"SetFogFar",          lua_GlueSetFogFar},
        {"SetFogColor",        lua_GlueSetFogColor},
        {"ClearFog",           lua_GlueClearFog},
        {"ResetLights",        lua_GlueResetLights},
        {"AddLight",           lua_GlueAddLight},
        {"AddCharacterLight",  lua_GlueAddCharacterLight},
        {"AddPetLight",        lua_GlueAddPetLight},
    };
    lua_getglobal(L, "__WoweeFrameMT");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
        return 1;
    }
    for (const auto& [name, func] : methods) {
        lua_pushcfunction(L, func);
        lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// The glue screens' own music and ambience
// ---------------------------------------------------------------------------

/// PlayGlueMusic(track) and PlayGlueAmbience(track, fadeSeconds).
///
/// Both name a row in SoundEntries.dbc rather than a file - "GS_LichKing" is
/// the WotLK title theme, "GlueScreenIntro" the wind under the login screen -
/// and the audio coordinator is what reads that table.
///
/// They were missing entirely, so the glue screens were silent: this client's
/// own login music belongs to its own login screen and is stopped the moment
/// the glue screens take the display, which left nothing playing at all.
int lua_PlayGlueMusic(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* ac = svc ? svc->audioCoordinator : nullptr;
    const char* track = luaL_optstring(L, 1, "");
    if (ac && track && *track) ac->playGlueMusic(track);
    return 0;
}

int lua_PlayGlueAmbience(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* ac = svc ? svc->audioCoordinator : nullptr;
    // CharacterSelect_OnShow indexes GlueAmbienceTracks with whatever model is
    // showing, and a model with no row there hands over nil rather than
    // skipping the call.
    const char* track = luaL_optstring(L, 1, "");
    const float fade = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    if (ac) ac->playGlueAmbience(track ? track : "", fade);
    return 0;
}

int lua_StopGlueAmbience(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->audioCoordinator) svc->audioCoordinator->stopGlueAmbience();
    return 0;
}

/// StopGlueMusic() - the glue screens' music, stopped.
///
/// GlueParent calls it on the way into the world, where the zone's own music
/// takes over. Missing, the login theme carried on playing under the world.
int lua_StopGlueMusic(lua_State* L) {
    auto* svc = getLuaServices(L);
    if (svc && svc->audioCoordinator) svc->audioCoordinator->stopGlueMusic();
    return 0;
}

/// LaunchURL(url) - the player's browser, on the address the interface names.
///
/// Manage Account and Community Site on the login screen are this and nothing
/// else, and so is Technical Support on character select; without it those
/// three buttons were dead. The address comes from GlueStrings, but it reaches
/// here as a Lua string like any other, so it goes through the same check a
/// chat link does - http(s) only, plain ASCII, and never through a shell.
int lua_LaunchURL(lua_State* L) {
    const char* url = luaL_optstring(L, 1, "");
    if (!url || !*url) return 0;
    if (!core::openExternalUrl(url)) {
        LOG_WARNING("LaunchURL refused '", url, "'");
    }
    return 0;
}

/// Where the camera sits around the model, in degrees.
///
/// Read back as it was set. Nothing turns yet, and this does not claim
/// otherwise - but the rotate buttons and the click-drag both do arithmetic on
/// what they read, so an angle that is not a number takes the handler down on
/// the first frame the button is held.
int lua_GetCharacterSelectFacing(lua_State* L) {
    lua_pushnumber(L, selection().selectFacing);
    return 1;
}

int lua_SetCharacterSelectFacing(lua_State* L) {
    selection().selectFacing = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    return 0;
}

int lua_GetCharacterCreateFacing(lua_State* L) {
    lua_pushnumber(L, selection().createFacing);
    return 1;
}

int lua_SetCharacterCreateFacing(lua_State* L) {
    selection().createFacing = static_cast<float>(luaL_optnumber(L, 1, 0.0));
    return 0;
}

}  // namespace

void registerGlueLuaAPI(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction func;
    } api[] = {
        // Login
        {"GetClientExpansionLevel", lua_GetClientExpansionLevel},
        {"GetSavedAccountName",     lua_GetSavedAccountName},
        {"SetSavedAccountName",     lua_SetSavedAccountName},
        {"GetSavedAccountList",     lua_GetSavedAccountList},
        {"SetSavedAccountList",     lua_SetSavedAccountList},
        {"GetUsesToken",            lua_GetUsesToken},
        {"SetUsesToken",            lua_SetUsesToken},
        {"IsTrialAccount",          lua_False},
        {"IsStreamingTrial",        lua_False},
        {"EULAAccepted",            lua_NothingPending},
        {"TOSAccepted",             lua_NothingPending},
        {"IsScanDLLFinished",       lua_NothingPending},
        // The rest of the agreements AccountLogin_ShowUserAgreements walks,
        // answered the way the two above are: already agreed to, so the
        // dialog is not raised. Missing, each one read as nil - "not accepted"
        // - and the login screen queued a notice the player cannot dismiss
        // because nothing here records the dismissal.
        {"TerminationWithoutNoticeAccepted", lua_NothingPending},
        {"ScanningAccepted",        lua_NothingPending},
        {"ContestAccepted",         lua_NothingPending},
        // Whether to raise the "these settings changed" notice. Nothing here
        // changes a setting behind the player's back, so there is nothing to
        // warn about, and ChangedOptionsDialog_OnShow hides itself on false.
        {"ShowChangedOptionWarnings", lua_False},
        // Whether this machine meets the client's requirements.
        //
        // AccountLogin.xml raises "This system will not be supported in future
        // versions of World of Warcraft" when this is false, and missing it
        // read as false - so the login screen opened behind that notice on
        // every machine. It became visible the moment GetBoundsRect was
        // implemented, because GlueDialog_Show had been raising on that call
        // and abandoning the dialog before it could be shown.
        {"IsSystemSupported",       lua_NothingPending},
        {"DefaultServerLogin",      lua_DefaultServerLogin},
        {"CancelLogin",             lua_CancelLogin},
        {"StatusDialogClick",       lua_StatusDialogClick},
        {"QuitGame",                lua_QuitGame},
        {"SetCurrentScreen",        lua_SetCurrentScreen},

        // Realm list
        {"GetServerName",           lua_GetServerName},
        {"GetRealmCategories",      lua_GetRealmCategories},
        {"GetSelectedCategory",     lua_GetSelectedCategory},
        {"GetNumRealms",            lua_GetNumRealms},
        {"GetRealmInfo",            lua_GetRealmInfo},
        {"RealmListUpdateRate",     lua_RealmListUpdateRate},
        {"RequestRealmList",        lua_RequestRealmList},
        {"ChangeRealm",             lua_ChangeRealm},
        {"IsInvalidLocale",                lua_FalseCategoryTest},
        {"IsTournamentRealmCategory",      lua_FalseCategoryTest},
        {"IsInvalidTournamentRealmCategory", lua_FalseCategoryTest},

        // Character select
        {"GetNumCharacters",        lua_GetNumCharacters},
        {"GetCharacterInfo",        lua_GetCharacterInfo},
        {"SelectCharacter",         lua_SelectCharacter},
        {"EnterWorld",              lua_EnterWorld},
        {"DeleteCharacter",         lua_DeleteCharacter},
        {"GetCharacterListUpdate",  lua_GetCharacterListUpdate},
        {"DisconnectFromServer",    lua_DisconnectFromServer},
        {"GetSelectBackgroundModel", lua_GetSelectBackgroundModel},
        {"GetCreateBackgroundModel", lua_GetCreateBackgroundModel},

        // Character creation
        {"GetAvailableRaces",       lua_GetAvailableRaces},
        {"GetAvailableClasses",     lua_GetAvailableClasses},
        {"GetSelectedRace",         lua_GetSelectedRace},
        {"SetSelectedRace",         lua_SetSelectedRace},
        {"GetSelectedClass",        lua_GetSelectedClass},
        {"SetSelectedClass",        lua_SetSelectedClass},
        {"GetSelectedSex",          lua_GetSelectedSex},
        {"SetSelectedSex",          lua_SetSelectedSex},
        {"IsRaceClassValid",        lua_IsRaceClassValid},
        {"GetFactionForRace",       lua_GetFactionForRace},
        {"GetNameForRace",          lua_GetNameForRace},
        {"CycleCharCustomization",  lua_CycleCharCustomization},
        {"RandomizeCharCustomization", lua_RandomizeCharCustomization},
        {"ResetCharCustomize",      lua_ResetCharCustomize},
        {"CreateCharacter",         lua_CreateCharacter},

        // The glue screens' sound. Both name a SoundEntries row.
        {"PlayGlueMusic",               lua_PlayGlueMusic},
        {"PlayGlueAmbience",            lua_PlayGlueAmbience},
        {"StopGlueAmbience",            lua_StopGlueAmbience},
        {"StopGlueMusic",               lua_StopGlueMusic},
        {"LaunchURL",                   lua_LaunchURL},

        // The model frames. Which frame holds the scene, which scene it holds
        // and everything the screen says about it are recorded here and drawn
        // by the client - see rendering::GlueScene. The facing pair below is still
        // recorded only.
        //
        // The methods themselves go on the frame metatable, which does not
        // exist yet at this point; the client installs them through the call
        // below before it loads GlueXML.
        {"__WoweeInstallGlueModelMethods", lua_GlueInstallModelMethods},
        {"SetCharSelectModelFrame",     lua_SetCharSelectModelFrame},
        {"SetCharCustomizeFrame",       lua_SetCharCustomizeFrame},
        {"SetCharSelectBackground",     lua_SetCharSelectBackground},
        {"SetCharCustomizeBackground",  lua_SetCharCustomizeBackground},
        {"GetCharacterSelectFacing",    lua_GetCharacterSelectFacing},
        {"SetCharacterSelectFacing",    lua_SetCharacterSelectFacing},
        {"GetCharacterCreateFacing",    lua_GetCharacterCreateFacing},
        {"SetCharacterCreateFacing",    lua_SetCharacterCreateFacing},
    };
    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

}  // namespace wowee::addons
