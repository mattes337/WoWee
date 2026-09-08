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

TEST_CASE("stock alert shine duration sums ordered stages not parallel animations", "[animation]") {
    AnimationFixture f;
    f.run(R"lua(
        -- AlertFrames.xml shine animIn: .2 stage then .85 parallel stage.
        local g = __WoweeCreateAnimationGroup(frame)
        local fadeIn = g:CreateAnimation('Alpha')
        fadeIn:SetDuration(0.2)
        fadeIn:SetOrder(1)
        local slide = g:CreateAnimation('Translation')
        slide:SetDuration(0.85)
        slide:SetOrder(2)
        local fadeOut = g:CreateAnimation('Alpha')
        fadeOut:SetStartDelay(0.35)
        fadeOut:SetDuration(0.5)
        fadeOut:SetOrder(2)
        assert(g:GetDuration() == 0.2 + 0.85, 'ordered stock shine span must total 1.05')
        -- Observation only: do not lock the existing parallel tick defect
        -- into a passing assertion. Duration correctness is the gate above.
        g:Play()
        __WoweeTickAnimations(0.85)
        print('PORT07 ordering observation: duration=' .. g:GetDuration() ..
              ' playing_after_0.85=' .. tostring(g:IsPlaying()))
    )lua");
}

TEST_CASE("duration groups sparse order values and recomputes changed spans", "[animation]") {
    AnimationFixture f;
    f.run(R"lua(
        local g = __WoweeCreateAnimationGroup(frame)
        assert(g:GetDuration() == 0)
        local late = g:CreateAnimation('Animation')
        late:SetOrder(9)
        late:SetDuration(2)
        local early = g:CreateAnimation('Animation')
        early:SetOrder(3)
        early:SetDuration(1)
        early:SetStartDelay(0.25)
        local parallel = g:CreateAnimation('Animation')
        parallel:SetOrder(9)
        parallel:SetDuration(0.5)
        assert(g:GetDuration() == 3.25, 'missing order numbers are not time slots')
        parallel:SetDuration(4)
        assert(g:GetDuration() == 5.25)
        parallel:SetOrder(3)
        assert(g:GetDuration() == 6)
    )lua");
}
