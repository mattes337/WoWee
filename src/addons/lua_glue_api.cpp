// lua_glue_api.cpp - what the login and character screens call.
//
// GlueXML is a separate interface from FrameXML with a separate vocabulary:
// the world's globals are not published to it and its own are not published to
// the world. What is here is only what the glue screens actually call - read
// out of the failures a real load produced, not guessed at.
//
// What is real here and what is not:
//
//   * The expansion level is real, read from the active profile - the glue
//     screens gate the character-creation races and the login art on it.
//   * The character count is real, off the character list the world handler
//     holds.
//   * The saved account name is real, kept with wowee's own configuration
//     rather than in the original client's WTF, so launching either client
//     does not overwrite what the other saved.
//   * The model frames are recorded and nothing more. The glue screens hand
//     the client a frame to draw a character into; drawing into it is the
//     renderer's half and is not here, so the name is remembered and can be
//     asked for rather than being dropped on the floor.
//
// Anything a glue screen calls that is not here answers through the
// missing-API fallback, which records the name - so the gap stays visible.

#include "addons/lua_api_helpers.hpp"
#include "addons/lua_api_registrations.hpp"
#include "core/config_paths.hpp"
#include "game/expansion_profile.hpp"

#include <lua.h>
#include <lauxlib.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace wowee::addons {

namespace {

/// Where the account name the login screen remembers is kept.
///
/// Deliberately not the original client's WTF: launching either client must
/// not overwrite what the other saved, which is the plan's own rule for saved
/// state and the reason saved variables moved here too.
std::string savedAccountPath() {
    return core::getConfigRoot() + "/glue_account.cfg";
}

std::string readSavedAccount() {
    std::ifstream in(savedAccountPath());
    if (!in) return {};
    std::string name;
    std::getline(in, name);
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n')) {
        name.pop_back();
    }
    return name;
}

/// The expansion, counted from zero: 0 vanilla, 1 TBC, 2 Wrath.
///
/// The same numbering the world interface's GetAccountExpansionLevel uses, and
/// what CharacterCreate reads to decide whether the death knight and the two
/// TBC races are offered.
int expansionLevel(lua_State* L) {
    auto* svc = getLuaServices(L);
    auto* reg = svc ? svc->expansionRegistry : nullptr;
    auto* prof = reg ? reg->getActive() : nullptr;
    if (!prof) return 2;
    if (prof->id == "wotlk") return 2;
    if (prof->id == "tbc") return 1;
    return 0;  // classic and turtle
}

int lua_GetClientExpansionLevel(lua_State* L) {
    lua_pushnumber(L, expansionLevel(L));
    return 1;
}

int lua_GetNumCharacters(lua_State* L) {
    auto* gh = getGameHandler(L);
    lua_pushnumber(L, gh ? static_cast<double>(gh->getCharacters().size()) : 0.0);
    return 1;
}

int lua_GetSavedAccountName(lua_State* L) {
    const std::string name = readSavedAccount();
    if (name.empty()) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, name.c_str());
    }
    return 1;
}

int lua_SetSavedAccountName(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    std::error_code ec;
    std::filesystem::create_directories(core::getConfigRoot(), ec);
    std::ofstream out(savedAccountPath(), std::ios::trunc);
    if (!out) {
        LOG_WARNING("Glue: could not write ", savedAccountPath());
        return 0;
    }
    out << (name ? name : "") << "\n";
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

/// The frame the glue screens want a character drawn into.
///
/// Recorded under a name the client can read back, and nothing more: the
/// drawing is the renderer's half. A no-op would lose which frame was asked
/// for, and the glue screens set it once and never ask again.
int rememberModelFrame(lua_State* L, const char* key) {
    lua_pushvalue(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, key);
    return 0;
}

int lua_SetCharSelectModelFrame(lua_State* L) {
    return rememberModelFrame(L, "wowee_charselect_model_frame");
}

int lua_SetCharCustomizeFrame(lua_State* L) {
    return rememberModelFrame(L, "wowee_charcustomize_model_frame");
}

}  // namespace

void registerGlueLuaAPI(lua_State* L) {
    const struct {
        const char* name;
        lua_CFunction func;
    } api[] = {
        {"GetClientExpansionLevel", lua_GetClientExpansionLevel},
        {"GetNumCharacters",        lua_GetNumCharacters},
        {"GetSavedAccountName",     lua_GetSavedAccountName},
        {"SetSavedAccountName",     lua_SetSavedAccountName},
        {"IsTrialAccount",          lua_False},
        {"IsStreamingTrial",        lua_False},
        {"SetCharSelectModelFrame", lua_SetCharSelectModelFrame},
        {"SetCharCustomizeFrame",   lua_SetCharCustomizeFrame},
    };
    for (const auto& [name, func] : api) {
        lua_pushcfunction(L, func);
        lua_setglobal(L, name);
    }
}

}  // namespace wowee::addons
