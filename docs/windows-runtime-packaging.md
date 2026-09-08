# Windows runtime DLL packaging

Windows CI currently uses MSYS2 `ldd` to copy resolved non-system dependencies
beside `build/bin/wowee.exe` before CPack. That single-configuration workflow is
preserved. Native Visual Studio builds place the executable under `bin/Debug`
or `bin/Release`, so dependencies must be bundled in that same directory.

`cmake/WindowsRuntime.cmake` now installs only the top-level DLLs next to the
selected `wowee` executable, flat into the installation's `bin` directory.
Discovery happens at install time, so DLLs copied after configure are included.
Installing Release cannot copy Debug dependencies or recreate Debug/Release
subdirectories. This change does not automatically discover or bundle DLLs.

Run `python tools/test_windows_runtime_install.py` to exercise the real CMake
helper with Ninja and Ninja Multi-Config. The fixture checks separate Debug/
Release outputs, shared output directories, post-configure DLL additions,
non-DLL exclusion and nested-DLL exclusion. Ninja must be available on PATH or
in the standard Visual Studio CMake tools location. On September 8, 2026 all
three fixture scenarios passed locally; the Windows headless CI job also runs
this check. A real packaged-client launch remains a separate acceptance gate.


For native MSVC builds, the explicit bundler resolves the executable's PE import
graph with `dumpbin` and copies only the non-system DLL closure. It searches the
provided directories in order, then already-staged DLLs. Use directories for the
selected configuration; for a Debug build, put the debug prefix first:

```powershell
python tools/windows/bundle_runtime_dlls.py --exe build/bin/Debug/wowee.exe `
  --search-dir dependency-prefix/debug/bin --search-dir dependency-prefix/bin
```

The script discovers Visual Studio's x64 dumpbin or accepts `--dumpbin`. It
resolves the entire import graph before copying, reports unresolved imports as
errors, and leaves Windows/System32 dependencies and API sets to the operating
system. It does not launch the client. Dynamic `LoadLibrary` dependencies and
plugins do not appear in the import graph; CMake still stages Vulkan separately.
System/CRT availability on a destination machine remains a deployment check.

Run `python tools/test_windows_runtime_bundle.py` for parser, case-insensitive
transitive graph, search-order and unresolved-dependency tests, plus a real
Windows fixture that builds an executable importing a two-DLL chain. On September
8, 2026 all four tests passed and both real DLLs were copied byte-identically
beside the fixture executable, without starting it. The Windows headless CI job
runs both packaging regressions.

## September 8 local installed-package verification

A full Debug install completed into `logs/fork-baseline/package-verify` after
building its declared tool prerequisites (`dbc_to_csv`, `auth_probe`,
`auth_login_probe`, `blp_convert`). The first attempt, before those prerequisites
were built, stopped at the missing tool; the completed install is recorded in
`logs/fork-baseline/package-install-complete.txt`. No install-rule workaround or
old executable was substituted. This configuration does not install framexml_run
or the disabled asset_extract target.

The installed client matched released binary SHA-256
`4b4c8c78559e3aab51015652a9db2015de704cf48cc5f60c00a4d1e9dd355005`.
All 81 compiled shader binaries matched the installed overlay byte-for-byte.
The tree contains five executables and four DLLs flat beside them, with no
Debug/Release subdirectories or symlinks. Dumpbin import closure resolved every
installed executable using only the package directory plus system DLL
classification: four non-system DLLs for wowee, one each for the auth tools,
and none for dbc_to_csv/blp_convert. No dependency-prefix directory was searched.

The [sanitized package smoke result](evidence/windows-package-smoke.json)
records exact executable/DLL, PNG and log hashes. The real client ran with its
working directory set to installed `bin`, using those installed assets, a fresh
config root and explicit extracted-data fixture. Child PATH contained only
installed bin, Windows/System32 and Windows. Required Vulkan validation was
loaded from the explicitly supplied SDK path. The 120-update run exited 0 with
normal quit, no ERROR/FATAL entries and exactly one screenshot success event.
Pillow verified and fully decoded the capture as 1280x720 RGBA. Original output
is under `logs/fork-baseline/package-smoke`; the executed client wrote its log
under the installed bin and a copy was retained with the fixture evidence.

This proves a local installed-package startup and readback without build-tree
asset/import fallback. It does not prove a clean destination machine: the host
provides system/CRT DLLs, graphics drivers and the validation SDK; extracted game
data is explicitly external. It also does not certify Release packaging,
authenticated world rendering, optional dynamically loaded plugins, or execution
of the four installed command-line tools.
