# Stale FrameXML emitter runner evidence — 2026-09-08

## Scope

A stock runner integration attempted to verify commit `16c377939` by asserting
that `AnimTimerFrameCountdownAnimGroup:IsPlaying()` immediately after FrameXML
load and before any explicit animation tick. The assertion failed. This note
distinguishes a missing runtime dispatch from a stale executable; it does not
establish why the stale object was selected or built.

## Frozen inputs

The executable was
`G:/WoW Projects/wowee/build-fork-windows/bin/Debug/framexml_run.exe`, SHA-256
`be0ec04c26bf5655f2223e1cbb0b90d510b605ee382574c25d09d7f52f8f5357`.
It reported source
`72a2d495318006a739206fb3060d108055f815a4-dirty`. That commit contains
`16c377939`, but the source label alone does not prove which contents were
compiled into each translation unit.

The only `AnimTimerFrame.xml` beneath the fixture interface tree was
`C:/wowee-runner-input-animation-20260908/assets/interface/FrameXML/AnimTimerFrame.xml`,
SHA-256
`d01349cae6ae5a144a27704b30a40aaa564b5188d5ee40934fb08e35a0955935`.
It contains the stock group `OnLoad` body `self:Play()`.

## Runtime and emitted-Lua evidence

A fresh invocation used the same frozen executable and assets with separate
config, log, and FrameXML dump directories. Before any `--tick`, a real-Lua
diagnostic observed:

```text
playing=false done=true duration=15 looping=NONE elapsed=nil runId=nil
anim_elapsed=nil anim_progress=nil anim_finished=nil
group_onload=function child_onfinished=function registry=nil
```

The complete log is
`C:/wowee-animation-onload-live-diagnostic/stdout.log`, SHA-256
`bce8553bd3f8cdb8bda0cd53bceff3e7422d7ae4e01ae94d3477b776a504c596`.
Thus the handler and the complete 15-second child schedule existed, but `Play`
had never run.

A second fresh invocation called the installed handler directly with
`g:OnLoad()`. The call succeeded and changed `runId` from nil to 1,
`IsPlaying()` to true, and the playing-registry entry to true. Its log is
`C:/wowee-animation-onload-live-diagnostic-2/stdout.log`, SHA-256
`41fd9e80f442cd4832f47224ccb0d7dd84cc1bb7a12755c667ac6462c9be8bad`.

`WOWEE_FRAMEXML_EMIT_DIR` captured the Lua produced inside
`AddonManager::loadXmlFile` immediately after `emitFrameXml` and before Lua
execution. The resulting
`C:/wowee-animation-onload-emit-diagnostic/emit/AnimTimerFrame.xml.lua`,
SHA-256
`9ca0f19fca85c1227d8797c175abbb7a6fd842c7b851559fe5a64257fe69e402`,
contains the group `SetScript("OnLoad", ...)`, its generic child, the child's
`OnFinished`, and then the frame-level `__WoweeFireOnLoad`. It does **not**
contain commit `16c377939`'s group invocation:

```lua
if __w[2].OnLoad then __w[2]:OnLoad() end
```

The same dump set contains `Blizzard_Calendar.xml.lua` with
`CreateAnimation("Animation", "CalendarViewEventFlashTimer")` and
`SetSmoothing("OUT")`. The executable therefore contains the preceding generic
animation and smoothing emitter changes while omitting the later OnLoad
dispatch change.

## Incremental-build identity

The runner project names the source directly as
`G:/WoW Projects/wowee/src/ui/framexml_emitter.cpp`; its `Cl.items.tlog` maps
that path to
`build-fork-windows/framexml_run.dir/Debug/framexml_emitter.obj`. The runner
object was 2,189,580 bytes, had SHA-256
`e65ddc3409b4cbf217cda4d1d115243e06d1a278c50553d3f3dfb2d1e6fe9fcf`,
and completed at 2026-09-08 19:45:15 UTC. The final source was 94,251 bytes
with mtime 19:44:55 UTC, while commit `16c377939` was recorded at 19:46:01 UTC.
The separately compiled client object completed later, at 19:47:26 UTC, was
2,667,631 bytes, and had SHA-256
`92690c7fcdcf8f1571eae98daff28a0f05db85f68a4a2d9ee3edfc3677697de1`.

`CL.read.1.tlog` records `include/ui/framexml_emitter.hpp` as a dependency and
the generated project contains the correct shared-worktree source paths for
both the emitter and Lua engine. The runner CL read/write logs were rewritten
at 19:47:52 UTC and do not preserve the individual compiler start time. These
records rule out a stale configured source path, but cannot prove whether the
runner compiler read the translation unit while it was being edited. Because
the runner object timestamp is newer than the source mtime, a later ordinary
incremental build can validly skip that object even though the linked behavior
shows it lacks the final edit.

## Conclusion and forced-rebuild verification

The failed assertion exercised an executable with stale FrameXML emitter code.
It does not disprove the focused generated-Lua tests and requires no additional
source fix. The runner emitter object completed before the OnLoad commit landed,
but the available evidence does not establish the exact build-race mechanism.

The required forced rebuild was then performed. The replacement runner has
SHA-256
`716ab029949ec60eb4e07a7b4fde726b4aa459d41789370bcc12693c630f6534`
and reports source `2e8d6a92e5dca18ddffdd6ba8824c48d7b7b1b35`. It used a fresh
fixture-local config and the same manifest identity
`cd64a47d94f4ad0b9691d12ab2696e04de002b831789eab66303c0ac004d14de`.

The new emitted stock file is
`C:/wowee-runner-input-animation-02/emit/AnimTimerFrame.xml.lua`, SHA-256
`14141bb0c183f3a374d668995f26e757407e104db146d4dc3feeb8ca1153a6fe`.
It contains the missing group call after construction of
`AutoCompleteInfoDelayer` and its `OnFinished`, and before the owning frame's
`__WoweeFireOnLoad`:

```lua
if __w[2].OnLoad then __w[2]:OnLoad() end
__WoweeFireOnLoad(__w[1])
```

The original integration assertions then passed without an intervening runner
tick: the Calendar animation retained `OUT` smoothing and the stock countdown
group was playing. After an explicit 15.1-second animation tick, the group was
stopped, the generic child reported progress 1, and the owning frame retained
the externally assigned alpha 0.37. The log is
`C:/wowee-runner-input-animation-02/stock_and_valid.log`, SHA-256
`5199bc8ad33608d295127443a42798a775b27267b79da05783f0b9f9e97fe06e`.
The complete result manifest reports pass and has SHA-256
`addac7dc5038d9d8aadc8b2c4f542975e41fc8d97d4dfc55bd78b000a082a19f`.

This verifies the stock group OnLoad through the forced runner and retains the
first failed run as evidence of stale-object risk. The correction was a rebuild;
no additional source repair was needed.
