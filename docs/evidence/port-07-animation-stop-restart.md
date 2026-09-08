# PORT-07 bounded paused-stop-restart repair

A paused AnimationGroup could never advance after `Stop(); Play()`. `Pause()`
left `paused=true`, `Stop()` removed the group from the active registry but did
not clear that flag, and `Play()` reset elapsed/progress while preserving the
stale pause. The next `__WoweeTickAnimations` call skipped the restarted group
forever. This is a concrete lifecycle defect in the existing implementation.

The repair clears the pause flag in `Stop()`. It preserves ordinary pause/resume
elapsed time, stop-time alpha restoration, and the existing restart clock.
No new clock, immediate completion or parallel animation engine was introduced.
No donor code was copied; this change repairs the existing WoWee implementation,
so there is no donor-import provenance claim.

## Production code and deterministic regression

The existing Lua animation bootstrap was moved verbatim from `lua_engine.cpp`
to `include/addons/animation_group_lua.hpp`. LuaEngine executes that exact shared
literal through its existing bootstrap function. The following unrelated frame
methods retain their local frame-metatable binding in a separate bootstrap.
A decoded-string comparison against the pre-change source confirmed byte equality
of the extracted animation Lua except for the intended one-line pause reset.

`tests/test_animation_group_lua.cpp` executes the shared production literal in
vendored Lua 5.1, with only a small frame-property sink supplied by the fixture.
It invokes the real `__WoweeTickAnimations` with explicit deltas. Two cases cover:

- Play, advance 0.25, pause without advancing, stop and restore base alpha,
  restart at progress 0, advance 0.5 then another 0.5, and complete once. Further
  ticks do not create another completion.
- Pause/resume preserving elapsed 0.5, stopped groups remaining inert, and a
  paused/stopped group being able to play and finish again.

Before the one-line repair, both cases failed in real Lua: the restarted clock
remained paused. After repair, MSVC Debug CTest passed **10 Catch assertions in
2 cases**, including the Lua lifecycle assertions within those script calls.
No fixed sleeps, server or GPU were involved. Local transcripts are
`build-headless-20260908/animation-before.txt`, `animation-after-build.txt` and
`animation-after.txt`. This is a targeted run, not a rerun of the full suite.
The new `animation_group_lua` CTest is shared by headless and normal builds and
links the same vendored Lua library. The earlier frozen 24-test full-suite
results remain evidence for their original snapshot, not this added target.

## Remaining PORT-07 scope

The source audit also found work beyond this small repair: order is stored but
the tick loop advances all animations together; GetDuration uses a maximum of
startDelay+duration rather than ordered spans; endDelay is stored but not used
by the tick; easing is exposed through GetSmoothProgress while interpolation
uses raw progress; repeated explicit Finish calls are not guarded for exactly
once completion. Loop overshoot, zero-duration completion callbacks, callback
reentrancy, paused Play semantics and cleanup on hide/destruction/reload also
need dedicated tests before making broader behavior claims.

This change does not certify stock cast bars, pulse/fade visuals, animation
ordering, all finish paths or live frame cleanup. PORT-07 remains open for those
acceptance gates. Full client compilation and actual FrameXML behavior are
separate from this real-Lua deterministic regression.
