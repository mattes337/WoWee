#pragma once

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace wowee::addons {
namespace error_api {

inline constexpr const char* kHandler = "wowee_error_handler";
inline constexpr const char* kReporting = "wowee_reporting_error";

inline int defaultHandler(lua_State* L) { lua_settop(L, 1); return 1; }
inline int getHandler(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kHandler);
    return 1;
}
inline int setHandler(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, kHandler);
    return 0;
}

// Called before pcall unwinds the failing function, so an addon's error
// handler can call debugstack and still see the original source frames.
inline int reportError(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kReporting);
    const bool reporting = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    if (!reporting) {
        lua_pushboolean(L, 1);
        lua_setfield(L, LUA_REGISTRYINDEX, kReporting);
        lua_getfield(L, LUA_REGISTRYINDEX, kHandler);
        lua_pushvalue(L, 1);
        if (lua_pcall(L, 1, 0, 0) != 0) lua_pop(L, 1);
        lua_pushboolean(L, 0);
        lua_setfield(L, LUA_REGISTRYINDEX, kReporting);
    }
    lua_pushvalue(L, 1);
    return 1;
}

inline int secureCall(lua_State* L) {
    if (lua_type(L, 1) == LUA_TSTRING) {
        lua_pushvalue(L, 1);
        lua_rawget(L, LUA_GLOBALSINDEX);
        lua_replace(L, 1);
    }
    if (!lua_isfunction(L, 1)) return 0;
    const int arguments = lua_gettop(L) - 1;
    lua_pushcfunction(L, reportError);
    lua_insert(L, 1);
    const int result = lua_pcall(L, arguments, LUA_MULTRET, 1);
    lua_remove(L, 1);
    if (result != 0) { lua_pop(L, 1); return 0; }
    return lua_gettop(L);
}

inline int debugStack(lua_State* L) {
    const int start = std::max(1, luaL_optint(L, 1, 1));
    const int first = std::max(0, luaL_optint(L, 2, 12));
    const int last = std::max(0, luaL_optint(L, 3, 10));
    std::vector<std::string> frames;
    for (int level = start; level < 10000; ++level) {
        lua_Debug frame{};
        if (!lua_getstack(L, level, &frame) || !lua_getinfo(L, "Sln", &frame)) break;
        std::string line = frame.short_src;
        if (frame.currentline > 0) line += ":" + std::to_string(frame.currentline);
        if (frame.name) line += ": in function '" + std::string(frame.name) + "'";
        line += '\n';
        frames.push_back(std::move(line));
    }
    const auto top = std::min(frames.size(), static_cast<std::size_t>(first));
    const auto bottom = std::min(frames.size() - top, static_cast<std::size_t>(last));
    std::string out;
    for (std::size_t i = 0; i < top; ++i) out += frames[i];
    if (top + bottom < frames.size()) out += "...\n";
    for (std::size_t i = frames.size() - bottom; i < frames.size(); ++i) out += frames[i];
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

} // namespace error_api

inline void registerErrorApis(lua_State* L) {
    lua_pushcfunction(L, error_api::defaultHandler);
    lua_setfield(L, LUA_REGISTRYINDEX, error_api::kHandler);
    lua_register(L, "geterrorhandler", error_api::getHandler);
    lua_register(L, "seterrorhandler", error_api::setHandler);
    lua_register(L, "securecall", error_api::secureCall);
    lua_register(L, "debugstack", error_api::debugStack);
}

} // namespace wowee::addons
