#include <catch_amalgamated.hpp>
#include "addons/animation_group_lua.hpp"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <cstring>

namespace {
struct AnimationFixture {
    lua_State* state = luaL_newstate();
    AnimationFixture() {
        REQUIRE(state != nullptr);
        luaopen_base(state);
        luaopen_table(state);
        lua_settop(state, 0);
        run("__WoweeFrameMT = {}; function __WoweeSetAnimOffset(frame,x,y) frame.x=x frame.y=y end");
        run(wowee::addons::kAnimationGroupLua);
        // Only the frame property sink is a fixture. All group methods and
        // timing below are the exact production Lua bootstrap in the real VM.
        run(R"lua(
            frame = { alpha = 1 }
            function frame:GetAlpha() return self.alpha end
            function frame:SetAlpha(value) self.alpha = value end
            function frame:SetScale(value) self.scale = value end
            group = __WoweeCreateAnimationGroup(frame)
            animation = group:CreateAnimation('Alpha')
            animation:SetDuration(1)
            animation:SetFromAlpha(1)
            animation:SetToAlpha(0)
            finishes = 0
            group:SetScript('OnFinished', function() finishes = finishes + 1 end)
        )lua");
    }
    ~AnimationFixture() { lua_close(state); }
    void run(const char* script) {
        int status = luaL_loadbuffer(state, script, std::strlen(script), "@animation-regression.lua");
        if (!status) status = lua_pcall(state, 0, 0, 0);
        INFO((status ? lua_tostring(state, -1) : "Lua passed"));
        REQUIRE(status == 0);
    }
};
}

TEST_CASE("stopping a paused animation permits a fresh timed run", "[animation]") {
    AnimationFixture f;
    f.run(R"lua(
        group:Play()
        __WoweeTickAnimations(0.25)
        assert(animation:GetProgress() == 0.25)
        group:Pause()
        __WoweeTickAnimations(2)
        assert(animation:GetProgress() == 0.25)
        group:Stop()
        assert(not group:IsPlaying() and frame.alpha == 1 and finishes == 0)
        group:Play()
        assert(animation:GetProgress() == 0)
        __WoweeTickAnimations(0.5)
        assert(animation:GetProgress() == 0.5, 'stale pause blocks restarted clock')
        assert(frame.alpha == 0.5 and finishes == 0)
        __WoweeTickAnimations(0.5)
        assert(not group:IsPlaying() and finishes == 1)
        __WoweeTickAnimations(3)
        assert(finishes == 1)
    )lua");
}

TEST_CASE("pause resume preserves elapsed time while stopped runs remain inert", "[animation]") {
    AnimationFixture f;
    f.run(R"lua(
        group:Play()
        __WoweeTickAnimations(0.25)
        group:Pause()
        __WoweeTickAnimations(4)
        group:Resume()
        __WoweeTickAnimations(0.25)
        assert(animation:GetElapsed() == 0.5 and animation:GetProgress() == 0.5)
        group:Stop()
        __WoweeTickAnimations(4)
        assert(animation:GetProgress() == 0.5 and finishes == 0)
        group:Pause()
        group:Stop()
        group:Play()
        __WoweeTickAnimations(1)
        assert(finishes == 1 and not group:IsPlaying())
    )lua");
}
