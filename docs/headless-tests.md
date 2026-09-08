# Headless tests

`WOWEE_HEADLESS_TESTS_ONLY=ON` builds the real Catch2 packet, bit-packet,
spline interpolation/body/facing, widget layout, text-edit, escape-action,
XML parser/emitter/takeover, settings-panel layout, ready-check state and Lua
VM/API/snippet and animation-group behavior, input-trace parsing,
screenshot-request/capture scheduling, and model-replacement lifetime. These
same targets remain in normal client builds; their
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

## Latest combined frozen baseline, 2026-09-08

Committed source `2b1c97914ae4b16f910a3f3c48a63411469f7f11` was captured
with `git archive` and extracted read-only for all four runs. The 137,943,040
byte archive contains 2,500 files and has SHA-256
`69d1365b381401a243d3969cd3852117207820cd752eed12a7e3b388e3c548a2`.

| Executed suite | Windows MSVC Debug | Ubuntu 24.04 GNU Debug ASan + UBSan |
|---|---|---|
| Pure headless | **28/28 pass** | **28/28 pass** |
| With optional SDL events | **29/29 pass** | **29/29 pass** |

No tests were skipped. Both SDL tiers used `SDL_VIDEODRIVER=dummy`;
`input_trace_sdl` is the sole optional target. The full `widget_tree` target,
including the production wheel-dispatch extraction, passed in every suite; a
separate Windows invocation reports 449 assertions across 101 cases.

Linux used Ubuntu image
`sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517`,
GNU 13.3.0, `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Vulkan headers and `glslc`
were asserted absent before the pure configuration and both before and after
installing `libsdl2-dev` for the SDL configuration.

The portable manifest and complete CTest outputs are under
`C:/wowee-headless-baseline-2b1c97914/`; the entry point is `manifest.json`
(SHA-256 `babefaf00b8d73e4e00bfa346f2bff2169126fba256505ccc0cc348b9c519250`).
The 29-test Windows SDL configured inventory is
`windows-sdl-configured-ctest.json` (SHA-256
`bf40ab65f92b97108800df65ec1939f7c5c961fb49aea9014589ac9ea9616696`);
it records configuration, while the separate CTest transcript records execution.
[Tracked identities and output hashes](evidence/headless-baseline-2b1c97914-20260908.json)
allow the retained files to be checked independently. Native-array fix 255
and smoothing change f041 landed later and are explicitly outside this frozen
snapshot. These results cover headless CTest only; they do not certify a GPU,
FrameXML runtime, server or gameplay path.

## Previous combined frozen baseline, 2026-09-08

Committed source `915c8752feae5c4e1cbf475983cda07d568e0616` was captured once
with `git archive` and used for every result below. The archive SHA-256 is
`da535eafc069df02091af2661555547565684ed24d5b31d8acb10972265cba7d`.
The snapshot excluded the then-dirty generated/status files
`docs/capability-ledger.json`, `docs/capability-ledger.md`, and
`docs/evidence/headless-configured-tests-20260908.json`; none is covered by
this run.

| Executed suite | Windows MSVC Debug | Ubuntu 24.04 GNU Debug ASan + UBSan |
|---|---|---|
| Pure headless | **27/27 pass** | **27/27 pass** |
| With optional SDL events | **28/28 pass** | **28/28 pass** |

The configured inventories and executed CTest logs are separate artifacts.
Windows and Linux contain the same 27 pure names and the same 28 SDL names;
`input_trace_sdl` is the sole optional addition. Linux used
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, and asserted that Vulkan
headers and `glslc` were absent before both stages.

The portable relative-path manifest, source identity, inventories and logs are
under `C:/wowee-headless-baseline-915c8752f/`; the entry point is
`C:/wowee-headless-baseline-915c8752f/manifest.json`. Changes in `6993a86a6`
and `0b437c9dd` were committed later and are not covered by this snapshot;
their focused regressions remain separate evidence.

## Earlier combined frozen baseline, 2026-09-08

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
