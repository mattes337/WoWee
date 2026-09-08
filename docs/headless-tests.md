# Headless tests

`WOWEE_HEADLESS_TESTS_ONLY=ON` builds the real Catch2 packet, bit-packet,
spline interpolation/body/facing, widget layout, text-edit, escape-action,
XML parser/emitter/takeover, settings-panel layout, ready-check state and Lua
VM/API/snippet tests. These same targets remain in normal client builds; their
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

## Current local validation, 2026-09-08

Two fresh builds include the Lua error observer, FrameXML runner failure
contract, settings-panel literal fix and on-demand root-anchor geometry fix.
The configured CTest count comes from CMake/CTest, not a source-file estimate.

| Check | Windows | Minimal Linux container |
|---|---|---|
| Headless configure/build/CTest | MSVC 19.44.35224.0, Debug: **20/20 pass** | GNU 13.3.0, Debug, ASAN + UBSan: **19/19 pass** |
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
tests were skipped on either host. The Windows total includes the later
`update_limit` smoke-helper regression (35 assertions / 4 cases), added after
the fresh 19-test strict Linux run. That new helper has not been run on Linux
in this validation batch.

The Linux run used Ubuntu 24.04 image
`sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517`, a read-only
source mount, and only `ca-certificates git cmake make g++ libglm-dev python3`
installed. Both `/usr/include/vulkan/vulkan.h` and `/usr/bin/glslc` were asserted
absent, and Vulkan/VulkanHeaders package discovery was disabled. The entire
container ran with `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`; this fresh
run supersedes the earlier sanitizer run that used default UBSan recovery.
Its source HEAD at setup was `81e66a4477214271ec4410b16da9b67366bc7943`; the six
inventory regressions were then rerun after `8d506ed6f` in the same container.

The Windows build uses the pinned GLM 1.0.1 header-only installation recipe
from CI, with Windows SDK 10.0.26100.0 and Vulkan discovery disabled. It needs
no SDL/OpenSSL DLLs or game data. Saved local transcripts:

- `build-headless-ci-final/windows-configure.txt`
- `build-headless-ci-final/windows-build.txt`
- `build-headless-ci-final/windows-ctest.txt` (fresh 19-test run)
- `build-headless-ci-final/windows-ctest-with-update-limit.txt` (20-test run)
- `build-headless-ci-final/linux-ci-result.txt`
- `build-headless-ci-final/linux-inventory-result.txt`

Each Python regression runs as its own CI step, so a nonzero exit fails that
step on both platforms. The CVar check compiles the real production dispatcher
inside a small fixture; donor/data/scanner checks use synthetic inputs, without
reading the real donor or game assets. Windows runtime regressions use synthetic
DLL fixtures and a newly compiled dependency chain, without starting the client.

These are local results. Hosted GitHub Actions execution, deliberately injected
hosted-CI failure, GPU/FrameXML runtime workers and dedicated-emulator gameplay
jobs remain unverified under TEST-10. The headless suite does not certify a
rendered interface, real FrameXML startup or gameplay.
