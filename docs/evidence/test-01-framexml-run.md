# TEST-01 bounded FrameXML runner failure contract

The existing `framexml_run` used the production AddonManager and LuaEngine but
returned only the number of failing command-line expressions. FrameXML load,
load-on-demand addon, and login callback failures could be printed while the
process returned success. A missing FrameXML directory or manifest could also
return success because `loadAllAddons` discarded `loadFrameXml`'s result.

The runner now aggregates those phases and asset initialization into its exit
status, capped at 100. AddonManager's existing loading method returns aggregate
FrameXML/enabled-addon success; existing callers can continue ignoring the
return. A malformed existing Bindings.xml also contributes to load failure.

Input failures return nonzero before loading the interface: missing asset
directory, empty expression, empty `--lua:`, and missing/unreadable/empty
`--script:path`. Script files execute through LuaEngine::executeFile, so syntax
errors, assertion failures and ordinary Lua throws follow the production error
path. Unknown options and invalid tick counts fail. Tick counts are bounded to
36,000; Lua expressions and scripts use the engine's existing 5-second budget.

The run prints compiled source revision/build date, absolute asset root,
1920x1080 viewport, no-server identity, fallback setting, and SHA256 identities
for the requested TOC, active/fallback manifests and each executed script. A
manifest hash identifies that manifest, not every referenced asset. An
unresolvable identity path is explicitly printed unavailable.

## Executed regression

MSVC Debug `test_framexml_run_contract`: 14 assertions in 3 cases passed. It
exercises the production exit aggregation, overflow cap, empty expression
predicate, and actual filesystem missing/empty/nonempty script validation.

```powershell
cmake --build build-headless-20260908 --config Debug --target test_framexml_run_contract
ctest --test-dir build-headless-20260908 -C Debug -R '^framexml_run_contract$' --output-on-failure
```

## Remaining acceptance

The complete executable still needs fresh full-target compilation and CLI
execution against the selected interface. Run with `WOWEE_LUA_API_FALLBACK=0`
and confirm nonzero exits for missing assets/manifest/listed file/script, a
script containing `assert(false)`, a script containing `error('failure')`, and
a script containing `while true do end`. Verify a successful fixture separately;
the local interface may have genuine missing APIs that should now fail it.

This patch does not implement process-level watchdogs for native hangs, SDL
input, captures, network-authoritative waits, variable viewport selection,
per-run config directories, or server/account/character isolation. The existing
default tool config directory is shared between tool runs; callers still need
to set `WOWEE_CONFIG_ROOT` uniquely. TEST-01 remains open for those gates.
