# Standalone FrameXML CLI failure matrix

`tools/framexml_run_matrix.py` drives a newly built production `framexml_run`
binary. It does not provide a server or replace any game behavior. Every process
gets a fresh config directory, copied original Lua/XML/TOC and fonts, and no
copied SavedVariables. The fixture's manifest routes its unchanged asset entries
back to the original art with an explicit relative base path. This is needed
because AddonManager saves addon variables into the addon source directory,
independently of WOWEE_CONFIG_ROOT.

The driver records the executable hash, original manifest and interface source
hashes, each routed fixture manifest/script hash, subprocess command and exit,
duration, and complete output. It forces FrameXML on and API fallback off, runs
without a server, and imposes an external process timeout. The output directory
must be new; earlier evidence is never overwritten.

```powershell
python tools/framexml_run_matrix.py --binary build-fork-windows/bin/Debug/framexml_run.exe --assets Data/extracted --output output/framexml-cli-20260908 --timeout 120
```

Cases include a stock baseline, missing arguments/assets/scripts, empty
expressions/scripts, Lua syntax/assertion/throw/protected-throw failures, runaway
Lua timeout, unknown options, malformed tick count, missing asset manifest and
missing FrameXML TOC.

Classification requires the expected error marker as well as a nonzero exit.
Crashes and external timeouts fail the case. If the stock baseline fails, later
runtime negatives are inconclusive even if their expected message appears;
an unrelated startup failure cannot prove the requested negative exits properly.
Preflight errors remain independently testable. Any non-pass result makes the
matrix command fail, and `report.json` is saved after each completed process.

Driver regression: `python tools/test_framexml_run_matrix.py` passes five unit
tests covering false success, wrong failure, Windows/POSIX crashes, external
timeout, baseline-dependent classification, and fixture SavedVariables isolation
with original-asset routing.

## September 8 executed matrix

The fresh executable passed all 16 classifications. Binary SHA256:
`e6f59410afa02533a614a8c5cae87d1dd4b76c72d0bdccf9f20b789b5c6af5e2`.
The reported source was `27955f5869ef2a29e858aa3e1fbc341260449b13-dirty`.
Full input hashes, commands, durations and output locations are in
`framexml-cli-matrix-20260908.json`; raw output is retained under
`logs/fork-baseline/framexml-matrix`.

| Case | Exit | Observed result |
|---|---:|---|
| Scoped stock baseline | 0 | 13 Lua and 126 XML files, no load/login errors, 5 fonts |
| Missing arguments | 2 | Usage diagnostic |
| Missing asset directory | 2 | Missing-directory diagnostic |
| Empty expression | 2 | Empty-expression diagnostic |
| Empty inline Lua | 2 | Empty-expression diagnostic |
| Missing script | 2 | Missing/unreadable/empty script diagnostic |
| Empty script | 2 | Missing/unreadable/empty script diagnostic |
| Malformed Lua | 2 | Syntax error near `)` |
| Assertion | 2 | `MATRIX_ASSERTION` |
| Ordinary throw | 2 | `MATRIX_THROW` |
| Protected throw | 2 | `MATRIX_PROTECTED`, despite no-op addon error handler |
| Runaway Lua | 2 | Production engine's `runaway script aborted` diagnostic |
| Unknown runner option | 1 | Unknown-option diagnostic |
| Invalid ticks | 1 | Integer-range diagnostic |
| Missing asset manifest | 1 | Asset initialization unavailable |
| Missing FrameXML TOC | 48 | Load failure plus downstream missing-interface errors |

No external timeout or native crash counted as a successful negative. All
runtime negatives had a successful baseline and their own expected diagnostic.

### Defect found: stale Calendar exclusion

The tested runner attempted 21 LoD addons, all loaded, and excluded
`Blizzard_Calendar`. This is excluded coverage for EVAL-01/BOTH-04, not successful
full stock-addon coverage. The old guard at `tools/framexml_run.cpp:269` claimed
the client also refused it, but `lua_LoadAddOn` in
`src/addons/lua_system_api.cpp:3647-3669` explicitly allows it now and describes
the earlier restriction as obsolete. The runner's stale special case has been
removed. A fresh runner build then passed a new isolated fallback-off baseline:
all 22 LoD addons loaded, zero load/login errors, five fonts loaded, and the
explicit assertion in `framexml-calendar-loaded.lua` confirmed
`IsAddOnLoaded("Blizzard_Calendar")`. Exit was 0 after 22.421 seconds. Its binary
SHA256 was `3c064d2ab17a9db7e3b7ba9cedb3c20b671946d2dd8a1295578148283d93b904`;
the reported revision was `846300085db1f090dd07d5b2a4cb83ab3f49e3da-dirty`.
The complete record is `framexml-calendar-baseline-20260908.json`, with raw output
under `logs/fork-baseline/calendar-baseline`.

This closes the stale runner-exclusion defect for the tested local interface.
Calendar gameplay/server-backed writes and EVAL-01's broader rendered panel,
login, logout/reconnect and character scenarios remain untested. The original
16-case matrix above remains attributed to its original binary and scope.
