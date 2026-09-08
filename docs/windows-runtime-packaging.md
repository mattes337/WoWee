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
