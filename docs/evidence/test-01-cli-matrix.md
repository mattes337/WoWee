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
with original-asset routing. The real CLI matrix is pending
fresh binary availability; no executable results are claimed here yet.
