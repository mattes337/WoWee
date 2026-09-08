# FrameXML runner viewport setup

`framexml_run <assets> --viewport:1024x768 ...` selects the initial pixel
dimensions before FrameXML loads. The default remains 1920x1080. The same
dimensions feed the widget tree, ImGui display size, layout, drawing, mouse
coordinate conversion and offscreen diagnostics. This is initial setup, not
a runtime resize operation or a GPU screenshot comparison.

The option is global regardless of its position among commands. Dimensions
must be integers from 1 through 16384; malformed and duplicate options fail
before addon loading. Lua screen queries continue to use interface units,
including the existing 768-unit screen-height convention.

## Executed evidence

Before the change, the same 1024x768 option and Lua screen-size assertions
returned exit 3: unknown option and `VIEWPORT_WIDTH` failure. That runner
reported source `63be8ed58969ed4037aa39a5db0855c1bb5db85c`.
The original output is retained in
`logs/fork-baseline/viewport-before/stdout.log`, SHA256
`27a99a7af28435567b700f069c2b373903d2921f0d89c675418611cfc8525fe2`.

After the change, the focused contract test passes **59 assertions in four
cases**. The full client and runner build under MSVC Debug. The local build
uses `/W4`, `WOWEE_WARNINGS_AS_ERRORS=OFF` and
`CMAKE_EXE_LINKER_FLAGS_DEBUG=/debug /INCREMENTAL:NO`; the final option avoids
an incremental-link file that exhausted the workspace drive during a prior
attempt. Successful build output is retained in
`logs/fork-baseline/build-viewport-no-incremental.log`.

```powershell
ctest --test-dir build-headless-20260908 -C Debug -R '^framexml_run_contract$' --output-on-failure --no-tests=error
python tools/test_framexml_run_matrix.py
python tools/framexml_run_matrix.py --binary build-fork-windows/bin/Debug/framexml_run.exe --assets Data/extracted --output logs/fork-baseline/framexml-viewport-matrix --timeout 120
```

The real runner matrix passes **19/19 classifications**, including the default
baseline, 1024x768 Lua screen/root-width assertions, invalid/duplicate viewport
options and the existing failure scenarios. Every case uses copied interface
files and a fresh config; SavedVariables are not copied. The executable reports
`9a4d7b7b0e45c6f721ef9493bd0bcc4b2297d35f-dirty`; its SHA256 is
`7c463de0e2a24edf2414209d6654c20be786ae2161124e288f7102a209615611`.
[Exact inputs, commands and results](framexml-viewport-matrix-20260908.json).

Review additionally exposed a driver-only false-pass path for positive cases
that require a failed baseline. A regression rejects that path; all **six**
driver tests pass. The recorded matrix has a passing baseline, so this change
does not alter its outcomes.

The runtime viewport assertion covers screen queries and root width. Drawing,
mouse conversion and offscreen propagation were reviewed in source but do not
yet have non-default-viewport interaction or pixel assertions. FrameXML runs
offline here; the result does not certify gameplay or visual fidelity.
