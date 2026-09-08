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
