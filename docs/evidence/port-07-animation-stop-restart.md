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

## Ordered duration follow-up

Stock `Data/extracted/interface/FrameXML/AlertFrames.xml` defines a shine
`animIn` with a 0.2-second order-1 Alpha stage, then order-2 Translation lasting
0.85 seconds in parallel with an Alpha whose 0.35-second start delay plus
0.5-second duration also spans 0.85 seconds. The group duration is therefore
1.05 seconds. The old implementation returned only the largest individual
span, 0.85 seconds, because it ignored order boundaries.

`GetDuration()` now takes the maximum `startDelay + duration` within each order
and sums those order spans. Sorting the populated order keys makes the result
deterministic while treating sparse order numbers as stages rather than empty
time slots. The query recomputes from the animations on every call, so later
duration and order changes are reflected.

Two additional real-Lua cases cover the stock 1.05-second shape and parallel,
sparse, and mutated order spans. With the old maximum calculation they failed
at the stock-duration and sparse-order assertions (**18 passed, 2 failed**).
With the duration repair, the isolated MSVC Debug CTest passed **20 assertions
in 4 cases**.

## Ordered tick follow-up

The same schedule now supplies each populated order's start offset to the tick
loop. A group owns one elapsed clock; an animation receives only the portion
after its order's offset. This lets same-order animations advance in parallel,
holds later orders at zero, and carries a large frame delta across one or more
stage boundaries. `Play()` resets the group clock, pause leaves it unchanged,
and looping resets it with the existing per-animation clocks.

The stock-shine case now proves the group is still playing after 0.85 seconds
and finishes at 1.05 seconds. Two more real-Lua cases cover a two-stage tick
that overshoots the first boundary, a final tick larger than the remaining
duration, once-only animation and group completion callbacks, and parallel
animations where a start delay extends the order span. All six lifecycle,
duration, and sequencing cases pass under MSVC Debug CTest: **30 assertions in
6 test cases**. Before the scheduling repair, each of the three sequencing
cases failed against the shared production literal.

## Remaining PORT-07 scope

The source audit also found work beyond these repairs: endDelay is stored but
not used by the tick; easing is exposed through GetSmoothProgress while
interpolation uses raw progress; repeated explicit Finish calls are not guarded
for exactly once completion. Loop carryover, paused Play semantics and cleanup
on hide/destruction/reload also need dedicated tests before making broader
behavior claims.

## Proven remaining animation-clock defects

Three minimal executions of the shared production Lua literal made additional
clock defects concrete before the bounded repairs below:

- **Animation callback reentrancy.** A one-second animation whose `OnFinished`
  calls its group's `Play()` ends the tick with `IsPlaying() == false`, animation
  progress reset to 0, one animation callback and one group callback. The outer
  tick continues after the animation callback and calls `Finish()`, cancelling
  the replay. Replacing `Play()` with `Stop()` produces one `OnStop` and then one
  group `OnFinished` in the same tick.
- **Zero-duration completion.** An order-1 animation with start delay 0.5 and
  duration 0 reaches progress 1 at 0.5 seconds, and its order-2 successor runs
  and finishes, but the zero-duration animation's `OnFinished` count remains 0
  while the group's count reaches 1.
- **Loop carryover.** A one-second `REPEAT` group ticked once with 2.5 seconds
  remains playing with animation elapsed reset to 0, progress left at 1, and
  exactly one `OnLoop`. The 1.5 seconds beyond the first loop boundary are
  discarded rather than advancing the next iteration.

These fail-before observations do not establish new callback or loop semantics.
The zero-duration and reentrancy cases are handled by the bounded follow-ups
below. Loop carryover remains open.

## Zero-duration completion follow-up

The zero-duration branch now uses the same `finished` flag and animation
`OnFinished` call as the timed branch. It does not alter group completion,
looping, or callback reentrancy behavior.

The added real-Lua case places an instant animation after a 0.5-second start
delay in order 1 and a one-second animation in order 2. It verifies no early
callback at 0.25 seconds, one callback exactly at the 0.5-second order boundary,
no duplicate from a zero-length tick, the successor's half and full progress,
one group completion, and no callbacks from ticks after completion. Before the
repair, vendored Lua 5.1 CTest reported **34 passed, 1 failed** at the boundary
callback assertion. After the repair, it passes **35 assertions in 7 cases**.

## Animation-callback reentrancy follow-up

Each `Play()` now gives the group run a new identity. The tick records the run
it began processing and updates the parent or performs terminal group handling
only while that same run remains active. An animation callback can therefore
start a replacement run without the old tick immediately finishing it, or call
`Stop()` without the old tick painting over restored properties and then
calling the group's `OnFinished`. This uses the existing group clock and active
registry; it does not add another scheduler or change loop carryover.

Two vendored-Lua cases cover both paths. The replay callback removes itself,
calls `Play()`, and leaves the replacement at progress 0 with no group finish;
subsequent half and full ticks advance and finish that replacement normally.
The stop callback uses an Alpha animation and proves the base alpha is restored,
`OnStop` fires once, group `OnFinished` does not fire, and later ticks remain
inert. Without the run guard, CTest reported **43 passed, 2 failed**. With it,
the isolated MSVC Debug target passes **45 assertions in 9 cases**.

This change does not certify stock cast bars, pulse/fade visuals, every loop or
finish path, or live frame cleanup. PORT-07 remains open for those acceptance
gates. Full client compilation and actual FrameXML behavior are separate from
this real-Lua deterministic regression.

## Repeat and bounce carryover follow-up

The loop clock now stops at every crossed duration boundary, completes that
round's animations, invokes `OnLoop`, resets the round, and spends the remaining
delta on the next round. A 2.5-second tick on a one-second `REPEAT` group now
fires two animation completions and two loop callbacks, then leaves the third
round at elapsed/progress 0.5. The equivalent `BOUNCE` case reverses twice and
also ends at progress 0.5. Before this repair both cases fired once, reset to
elapsed 0, left progress at 1, and discarded 1.5 seconds.

Loop callbacks retain the lifecycle guard from the prior repair. `Stop()` ends
catch-up and restores the frame. `Play()` starts a new run at zero and discards
the old run's remaining delta. `Pause()` keeps the old run's remaining delta as
`pendingElapsed`; `Resume()` consumes it on a later tick. Changing looping to
`NONE` from `OnLoop` takes effect when the next boundary is reached. Negative,
NaN, and infinite external deltas are treated as zero rather than corrupting
the group clock.

Catch-up is deliberately bounded at 64 loop boundaries per group per external
tick. Excess time is retained in `pendingElapsed`, including across a pause,
and later ticks consume up to another 64 boundaries. This prevents a tiny loop
duration and large frame delta from running an unbounded number of Lua
callbacks in one frame, but means a badly backlogged visual can remain behind
wall-clock time for multiple frames. `Play()` intentionally clears that debt
because it creates a replacement run.

The isolated fail-before fixture is at
`C:/wowee-port07-loop-carryover`. Its two overshoot cases failed against the
shared pre-repair literal (**13 passed, 2 failed** under the filtered run).
With the repair, the full real-Lua fixture passes **75 assertions in 15 cases**,
including multiple boundaries, `REPEAT`, `BOUNCE`, Stop/Play/Pause and
SetLooping callbacks, bounded short-loop work, retained debt, and invalid
deltas. This deterministic coverage does not establish live visual parity or
close the other PORT-07 items listed above.

## Stock Calendar smoothing emission

The extracted stock interface has one concrete smoothing consumer.
`Blizzard_Calendar.xml` declares `CalendarViewEventFlashTimer` as a 0.7-second
generic animation with `smoothing="OUT"`; `Blizzard_Calendar.lua` passes that
animation's `GetSmoothProgress()` directly to the RSVP flash texture alpha.
The FrameXML emitter created and timed the animation but omitted its smoothing
attribute, so the existing runtime method returned linear progress. At the
halfway point that is 0.5 rather than the runtime's OUT-smoothed 0.75.

The emitter now quotes the XML value and passes it unchanged to
`SetSmoothing`. An exact stock-shaped regression covers `OUT`; a second case
pins preservation of nonuppercase spelling rather than adding parser-side
normalization. Before the fix, the focused emitter case produced the expected
animation and duration calls but failed its `SetSmoothing("OUT")` assertion
(**2 passed, 1 failed**). The isolated repaired FrameXML suite passes **321
assertions in 92 cases**, and the production-Lua smoothing fixture passes its
five assertions.

No extracted stock XML or Lua uses `endDelay`, `SetEndDelay`, or
`GetEndDelay`. The emitter currently does not pass an end-delay attribute and
the runtime does not include it in scheduling, but this audit found no stock
consumer that establishes the intended behavior. End-delay semantics therefore
remain open rather than being inferred from another implementation.

## Generic animation property isolation

The Calendar flash timer is also an extracted generic `<Animation>` consumer.
It is a clock: button `OnUpdate` scripts read its
smooth progress and apply that value to separate flash textures. The emitter
instead created it as kind `Alpha`. The runtime then wrote the animation
group's captured base alpha and a zero translation offset to its parent on
every tick, regardless of whether the group contained an Alpha or Translation
animation; `Stop()` restored both properties unconditionally as well. Thus a
generic timer could overwrite unrelated alpha and offset changes on its parent,
and a Translation-only group could overwrite unrelated alpha.

The emitter now preserves the generic `Animation` kind. The tick records
whether its animations actually own alpha or translation before applying each
property, and `Stop()` restores only properties represented by those animation
kinds. This leaves the existing Alpha and Translation calculations and the
callback/run guards unchanged.

In the isolated fail-before run, the stock-shaped emitter assertion found
`CreateAnimation("Alpha", "CalendarViewEventFlashTimer")` rather than the
generic kind. The real-Lua generic timer tick/Stop and Translation-only cases
both failed because parent state was overwritten (**8 passed, 2 failed** in
those fixture calls). The repaired isolated suites pass **321 assertions in 92
FrameXML cases** and **90 assertions in 18 animation cases**.

For comparison only, the sibling `wow-client` implementation was inspected but
not copied: its XML path skips generic Animation elements, while its runtime
applies properties only for exact named animation kinds. That implementation
does not establish the desired behavior; the stock Calendar declaration and
consumer above provide the source contract for this repair.

## Animation and group OnLoad dispatch

The active stock `FrameXML/AnimTimerFrame.xml` declares
`AnimTimerFrameCountdownAnimGroup` with a group `OnLoad` that calls
`self:Play()`. Its 15-second generic child, `AutoCompleteInfoDelayer`, calls
`GuildRoster()` from animation `OnFinished`. The emitter installed both scripts
but never invoked animation or group OnLoad. The final frame-level
`__WoweeFireOnLoad` could not compensate: it reads the frame's `__scripts`
table, while animation objects store scripts directly on their Lua tables.
Consequently the countdown group never entered the playing registry.

The emitter now invokes an animation's declared OnLoad after that animation's
attributes and scripts have been installed. It invokes the group's declared
OnLoad after every child animation has been created. This lets the stock group
start with its complete 15-second schedule. No OnUpdate dispatch or other
animation event behavior was added.

The regression executes emitted Lua in vendored Lua 5.1 with the production
AnimationGroup literal. It verifies animation OnLoad exactly once, then group
OnLoad exactly once, observes the complete child duration before Play, and
ticks 15 seconds to the child's OnFinished. A second generated-Lua case declares
the animation in a virtual template: declaring the template fires zero times,
and replaying it on two concrete frames fires once for each frame. Before the
repair, both cases failed because no OnLoad call was emitted (**7 passed, 2
failed**). The repaired isolated FrameXML suite passes **333 assertions in 94
cases**.

## Focused Linux sanitizer validation

A later frozen archive at
`72a2d495318006a739206fb3060d108055f815a4`, which contains the OnLoad commit
`16c377939`, was configured in headless-only mode on the same pinned Ubuntu
24.04 image used by the earlier baseline. The git archive SHA-256 is
`aa3f843069c3f4276b62141efcc39928c745416aa43b35aa15cb1cfcb6913087`.
GNU 13.3.0 built only `test_framexml` and `test_animation_group_lua` in Debug
with AddressSanitizer and UndefinedBehaviorSanitizer. Vulkan headers and
`glslc` were checked absent before and after installing the compiler, CMake,
and GLM prerequisites.

With `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, focused CTest passed **2/2**.
Direct execution reports **333 assertions in 94 FrameXML cases** and **85
assertions in 17 AnimationGroup Lua cases**. The log is
`C:/wowee-framexml-asan-72a2d4953/linux-framexml-animation-asan.log` (SHA-256
`15fef4a88d2b968e2313204368558b1b687d54dd0f43d5a79bb504cc18f33711`); its
manifest is `C:/wowee-framexml-asan-72a2d4953/manifest.json` (SHA-256
`890691795340bdded9459c148358c41abb20fcda6afb7e11721c4f9e99b957b0`).

This is focused validation of those two current targets. It does not replace,
extend, or restate the complete frozen headless baseline at `2b1c97914`, and it
does not claim that every test configured at the later source identity ran.
