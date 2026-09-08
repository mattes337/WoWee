#include <catch_amalgamated.hpp>
#include "addons/lua_error_api.hpp"
extern "C" {
#include "lualib.h"
}
#include <cstring>
#include <string>

namespace {
struct Fixture {
    lua_State* L = luaL_newstate();
    Fixture() {
        luaopen_base(L);
        luaopen_string(L);
        lua_settop(L, 0);
        wowee::addons::registerErrorApis(L);
    }
    ~Fixture() { lua_close(L); }
    void run(const char* text) {
        const int base = lua_gettop(L);
        int result = luaL_loadbuffer(L, text, std::strlen(text), "@error-api-regression.lua");
        if (result == 0) result = lua_pcall(L, 0, 0, 0);
        INFO((result ? lua_tostring(L, -1) : "Lua passed"));
        REQUIRE(result == 0);
        CHECK(lua_gettop(L) == base);
    }
};
}

TEST_CASE("securecall preserves nil arguments and multiple return values", "[lua-error-api]") {
    Fixture f;
    f.run(R"lua(
        function Named(a, b, c)
            assert(a == 10 and b == nil and c == 30)
            return nil, a + c, nil
        end
        local function check(...)
            assert(select('#', ...) == 3)
            local a, b, c = ...
            assert(a == nil and b == 40 and c == nil)
        end
        check(securecall(Named, 10, nil, 30))
        check(securecall('Named', 10, nil, 30))
        assert(select('#', securecall(function() end)) == 0)
        assert(select('#', securecall('MissingName')) == 0)
        assert(select('#', securecall(false)) == 0)
        assert(select('#', securecall()) == 0)
    )lua");
}

TEST_CASE("securecall contains errors and reports the original live stack", "[lua-error-api]") {
    Fixture f;
    f.run(R"lua(
        local previous = geterrorhandler()
        local reports, captured = 0, ''
        local handler = function(message)
            reports = reports + 1
            assert(string.find(message, 'deliberate failure', 1, true))
            captured = debugstack()
        end
        seterrorhandler(handler)
        local function explode() error('deliberate failure') end
        local function wrapper() explode() end
        assert(select('#', securecall(wrapper)) == 0)
        assert(reports == 1)
        assert(string.find(captured, 'error-api-regression.lua', 1, true))
        assert(string.find(captured, 'explode', 1, true))
        assert(geterrorhandler() == handler)
        seterrorhandler(previous)
        assert(geterrorhandler() == previous)
        assert(securecall(function() return 'still running' end) == 'still running')
    )lua");
}

TEST_CASE("throwing and reentrant error handlers do not poison subsequent calls", "[lua-error-api]") {
    Fixture f;
    f.run(R"lua(
        local previous = geterrorhandler()
        local count = 0
        local function badHandler(message)
            count = count + 1
            securecall(function() error('nested error') end)
            error('handler failure')
        end
        seterrorhandler(badHandler)
        securecall(function() error('first') end)
        securecall(function() error('second') end)
        assert(count == 2)
        assert(geterrorhandler() == badHandler)
        seterrorhandler(previous)
        assert(not pcall(seterrorhandler, false))
        assert(geterrorhandler() == previous)
        local token = {}
        local observed
        seterrorhandler(function(message) observed = message end)
        securecall(function() error(token) end)
        assert(observed == token)
    )lua");
}

TEST_CASE("debugstack honors source frames start and top/bottom limits", "[lua-error-api]") {
    Fixture f;
    f.run(R"lua(
        local function inner()
            local all = debugstack(1, 100, 100)
            assert(string.find(all, "function 'inner'", 1, true))
            assert(string.find(all, "function 'outer'", 1, true))
            local skip = debugstack(2, 100, 100)
            assert(not string.find(skip, "function 'inner'", 1, true))
            local top = debugstack(1, 1, 0)
            assert(string.find(top, "function 'inner'", 1, true))
            assert(not string.find(top, "function 'outer'", 1, true))
            assert(string.find(top, '...', 1, true))
            assert(debugstack(1000) == '')
        end
        local function outer() inner() end
        outer()
    )lua");
}

TEST_CASE("ordinary Lua failures still fail the harness", "[lua-error-api]") {
    Fixture f;
    CHECK(luaL_dostring(f.L, "error('unprotected regression failure')") != 0);
    REQUIRE(lua_isstring(f.L, -1));
    CHECK(std::string(lua_tostring(f.L, -1)).find("unprotected regression failure") != std::string::npos);
}
