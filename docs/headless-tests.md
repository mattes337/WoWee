# Headless tests

`WOWEE_HEADLESS_TESTS_ONLY=ON` builds the real Catch2 packet, bit-packet,
spline interpolation/body/facing, widget layout, text-edit, escape-action,
XML parser/emitter/takeover, settings-panel layout, ready-check state and Lua
VM/API/snippet, input-trace parser and screenshot-request state tests. These same targets remain in normal client builds; their
shared definition is `cmake/HeadlessTests.cmake`.

Only a C++20 toolchain, CMake 3.15+ and GLM are required. Catch2 and Lua 5.1.5 are vendored.
No Vulkan headers/loader, SDL, shader compiler, OpenSSL, game assets, window,
GPU or server is required. The client/editor and the remaining dependency-heavy
tests are deliberately excluded. This does not certify rendered UI or gameplay.

```sh
cmake -S . -B build-headless -DWOWEE_HEADLESS_TESTS_ONLY=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-headless --parallel 2
ctest --test-dir build-headless --output-on-failure --no-tests=error
```

On Windows use a fresh Visual Studio build directory and add `--config Debug`
to the build command and `-C Debug` to CTest. If GLM is not in the default search
path, supply `-DCMAKE_PREFIX_PATH=<dependency-prefix>` or `-Dglm_DIR=<glm-config-dir>`.
There is no required hardcoded vcpkg path. `WOWEE_BUILD_TESTS=OFF` together with
headless-only mode is rejected as a configuration error.

On GCC/Clang, add `-DWOWEE_ENABLE_ASAN=ON` for AddressSanitizer and UBSan.
The common logger object, Catch2 library and Lua VM are instrumented as well
as tests.
MSVC sanitizer support is unchanged and this option does not enable it there.

The independent `Headless tests` CI workflow uses an Ubuntu 24.04 container with
only compiler/CMake/GLM packages. It checks that Vulkan headers and glslc are
absent, disables Vulkan package discovery, builds with sanitizers, and runs every
configured test. The existing `Build` workflow retains graphics dependencies.
A separate Windows 2022 job builds the Debug suite using GLM 1.0.1 pinned to
commit `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`, installed as a header-only
CMake package. Both jobs run the source-identity regression and attach CTest
and configure diagnostics on failure. `ctest -L headless` selects this subset
in a full build as well. GPU/FrameXML runtime and local-emulator CI gates are
still separate, unverified work under TEST-10.

## Current frozen baseline, 2026-09-08

The updated ready-check completion/packet-serializer regression, screenshot
request outcomes and capture schedule, and input parser/SDL fixture were
validated together from committed source
`347dab9efb3319282100e71eeb19db60be695458` in fresh isolated builds.
At snapshot time `docs/fork-roadmap.md` and `tools/live_login_check.py` had tracked
local changes; both were excluded by `git archive`. They are not claimed as
covered by this run. Original transcripts from earlier runs remain preserved.

| Executed suite | Windows MSVC Debug | Ubuntu 24.04 GNU Debug ASan + UBSan |
|---|---|---|
| Pure headless | **23/23 pass** | **23/23 pass** |
| With optional SDL events | **24/24 pass** | **24/24 pass** |

Both subprocess sequences exited 0 and no tests were skipped. Linux applied
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, with sanitizer instrumentation
through the existing test configuration. It asserted Vulkan headers and glslc
absent before both pure and SDL stages. SDL dependencies were installed only
after the pure build/test. Windows used the isolated VS2022 x64 build and pinned
local GLM installation; its SDL stage used the existing vcpkg SDL2 package.

Saved evidence under `build-current-baseline/`:

- `identity.json`: source SHA and excluded tracked status.
- `windows-pure-ctest.txt` and `windows-sdl-ctest.txt`: executed Windows results.
- `linux-output.txt`, `linux-pure-last-test.log` and `linux-sdl-last-test.log`:
  Linux configure/build/test transcripts.
- `windows-configured-ctest.json` and `linux-configured-ctest.json`: CTest's
  configured inventories, with matching sets of 24 test names.
- `windows-portable-configured-ctest.json`: the same configured Windows inventory
  with source/build roots replaced by placeholders; commands are not executable
  until those placeholders are resolved. This inventory does not encode run status.

This rerun covers CTests only. Earlier Python/OpenSSL SRP evidence remains
separately scoped to its own snapshots; no hosted GitHub Actions, live ready-check
flow, presented-frame capture or gameplay success follows from these results.

## Earlier input-trace validation, 2026-09-08

The optional `-DWOWEE_TEST_SDL_EVENTS=ON` adds the real SDL queue fixture and
requires an SDL2 CMake package. It initializes SDL events only, with no video,
window or Vulkan. The default pure build still does not discover or need SDL.
Linux CI first builds/tests the pure suite, then installs `libsdl2-dev`, asserts
Vulkan headers/glslc remain absent, and builds/runs the optional fixture with
ASan and strict UBSan. Hosted execution of this workflow change is unverified.

Fresh isolated builds from committed snapshot
`779c216fb51493bb6cbb330b7538ad3ba6e8f18a` passed:

| Suite | Windows MSVC Debug | Ubuntu 24.04 GNU Debug ASan + UBSan |
|---|---|---|
| Default pure headless | 21/21 | 21/21 |
| With optional SDL events | 22/22 | 22/22 |

The Linux run used a read-only source mount, installed compiler/CMake/GLM
before the pure stage, and added SDL only afterward. Vulkan headers/glslc were
asserted absent at both stages; Vulkan package discovery stayed disabled.
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` applied to the entire run.
These runs include the input parser and actual queue test; they do not establish
OS polled input state, field focus, rendered UI outcomes or gameplay.

Local evidence is under `build-input-ci-validation/`: `identity.json`,
`windows-pure-ctest.txt`, `windows-sdl-ctest.txt`, `linux-output.txt`, and both
`linux-*-last-test.log` files. The later `screenshot_request` target was built
and passed separately on Windows (`windows-screenshot-ctest.txt`). For that
focused check, the isolated source overlaid only its header, test and shared
CMake module from `d80b4a3e9655f48fe6fe74787bb3584792a3d067`.
`windows-current-configured-ctest.json` inventories 23 configured tests after
that overlay; it is configuration evidence, not a combined 23-test execution.
The normal full build also registers the SDL target when SDL2 is available.

## Earlier full regression validation, 2026-09-08

Two fresh builds include the Lua error observer, FrameXML runner failure
contract, settings-panel literal fix and on-demand root-anchor geometry fix.
The configured CTest count comes from CMake/CTest, not a source-file estimate.

| Check | Windows | Minimal Linux container |
|---|---|---|
| Headless configure/build/CTest | MSVC 19.44.35224.0, Debug: **20/20 pass** | GNU 13.3.0, Debug, ASAN + UBSan: **20/20 pass** |
| Source identity regression | 1 pass | 1 pass |
| Capability scanner regressions | 6 pass | 6 pass |
| Capability inventory regressions | 6 pass | 6 pass |
| Opcode generator stability | 4 pass | 4 pass |
| Donor evidence capture | 7 pass, 1 explicit skip | 8 pass |
| Evaluation-data audit | 1 pass | 1 pass |
| CVar dispatcher compile/run | 13 checked cases pass | 13 checked cases pass |
| FrameXML CLI matrix classification/fixture | 5 pass | 5 pass |
| Windows runtime install fixture | 1 test / 3 generator-output scenarios pass | Windows job only |
| Native DLL bundler | 4 pass, including real compiled two-DLL chain | Windows job only |

The Windows donor skip is `test_symlink_source_is_rejected`: creating symlinks
was unavailable on this host. The corresponding Linux case passed. No CTest
tests were skipped on either host. Both totals now include `update_limit`,
the smoke-helper regression covering 35 assertions in 4 cases.

The Linux run used Ubuntu 24.04 image
`sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517`, a read-only
source mount, and only `ca-certificates git cmake make g++ libglm-dev python3`
installed. Both `/usr/include/vulkan/vulkan.h` and `/usr/bin/glslc` were asserted
absent, and Vulkan/VulkanHeaders package discovery was disabled. The entire
container ran with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; this fresh
run supersedes the earlier sanitizer run that used default UBSan recovery.
The latest full run, including all eight Python regression scripts and the
20 CTest targets, recorded source HEAD
`846300085db1f090dd07d5b2a4cb83ab3f49e3da` at setup.

The Windows build uses the pinned GLM 1.0.1 header-only installation recipe
from CI, with Windows SDK 10.0.26100.0 and Vulkan discovery disabled. It needs
no SDL/OpenSSL DLLs or game data. Saved local transcripts:

- `build-headless-ci-final/windows-configure.txt`
- `build-headless-ci-final/windows-build.txt`
- `build-headless-ci-final/windows-ctest.txt` (fresh 19-test run)
- `build-headless-ci-final/windows-ctest-with-update-limit.txt` (20-test run)
- `build-headless-ci-final/linux-ci-with-update-limit.txt` (fresh strict 20-test run and all Python checks)

Each Python regression runs as its own CI step, so a nonzero exit fails that
step on both platforms. The CVar check compiles the real production dispatcher
inside a small fixture; donor/data/scanner checks use synthetic inputs, without
reading the real donor or game assets. Windows runtime regressions use synthetic
DLL fixtures and a newly compiled dependency chain, without starting the client.

These are local results. Hosted GitHub Actions execution, deliberately injected
hosted-CI failure, GPU/FrameXML runtime workers and dedicated-emulator gameplay
jobs remain unverified under TEST-10. The headless suite does not certify a
rendered interface, real FrameXML startup or gameplay.


Independent review of the smoke failure contract in `2c9a20f1e`: malformed
limits throw, a completed iteration is counted after update/render/swap return,
and success requires both the requested count and observed SDL_QUIT dispatch.
Early loop exits, filtered/error quit enqueue, GPU device loss in smoke mode,
and update/render/swap exceptions fail through the executable's nonzero exit
path. This checks completed loop iterations, not presented frames, and does not
turn preexisting Lua/runtime warnings into a gameplay certification.
