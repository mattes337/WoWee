# MSVC source-identity dependency repair — 2026-09-09

## Reproduction

A stable-source Windows build generated
`build-fork-windows/generated/core/version.hpp` with revision
`496d69bb10efd321f5fcb972197649190df80dec` at 06:54:22 UTC and linked a new
`wowee.exe` at 06:54:36 UTC. The executable nevertheless contained and reported
`2e8d6a92e5dca18ddffdd6ba8824c48d7b7b1b35`; its SHA-256 was
`11c57ac6a829970fdcab3bc949b554e9ed74f4d0347788ffa6e57a5d1e5da616`.
The offline log is `D:/wowee-recovery-offline-20260909/stdout.log`.

`main.obj` had not changed since 2026-09-08 20:05:26 UTC and contained the old
revision literal. `Cl.items.tlog` still mapped `src/main.cpp` to that object, but
the current `CL.read.1.tlog` had no dependency section for `main.cpp`; its only
record of the generated version header belonged to the newly compiled
`lua_system_api.cpp`. The link therefore selected the stale inline
`kSourceRevision` COMDAT from `main.obj` even though another translation unit
had read the current header. The build log
`logs/fork-baseline/build-recovery-client-20260909.log` likewise records no
recompile of `main.cpp`, `auth_screen.cpp`, or `settings_panel.cpp`.

This stable-source reproduction rules out a Git commit landing during the build.
The defect was reliance on MSVC's compiler read tlog to retain every generated
header edge, combined with identical inline definitions spread across multiple
objects.

## Repair

Commit `b0a68d1e5` replaced the generated inline header with a stable declaration
header and one generated `version.cpp`. Both `wowee` and `framexml_run` compile
that generated source exactly once and call its out-of-line accessors. A changed
revision now changes a direct compiler source input, and one object exclusively
owns the version strings.

## Verification

`python tools/test_source_identity.py` passed its isolated Git-history cases:
unknown source, clean and dirty revisions, staged changes, reachable tags,
untracked-file exclusion, and no timestamp change for identical output.

An isolated Visual Studio 17 project then compiled a generated `version.cpp`
whose accessor returned `first`. After deleting `CL.read.1.tlog`, regenerating
the source to return `second`, and rebuilding serially, MSBuild compiled
`version.cpp`; the linked executable contained and printed only `second`.

A separate configure of the full repository completed successfully. The emitted
`wowee.vcxproj` and `framexml_run.vcxproj` each contained exactly one
`generated/core/version.cpp` `ClCompile` item. No shared client or runner binary
was built for this verification.
