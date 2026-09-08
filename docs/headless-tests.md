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

## Local validation, 2026-09-08

MSVC 19.44.35224.0, Windows SDK 10.0.26100.0, fresh
`build-headless-20260908`, Debug: configure/build succeeded; all 10 initial
CTest tests passed with Vulkan and VulkanHeaders discovery disabled. GLM was
resolved from the retained local dependency prefix. This execution required no
Vulkan headers, shader compiler, DLLs from SDL/OpenSSL, game data or GPU context.
The original spline-body fixture included an unused Application singleton stub;
removing that dead dependency lets its real parsing assertions compile headlessly.

The full Windows client remains outside this validation. A follow-up fix splits
the oversized raw literals in `addon_lua_snippets.hpp` into adjacent tokens to
avoid MSVC C2026 while preserving all 16 concatenated snippet strings byte for
byte. The settings-panel layout fixture now builds and passes in headless MSVC
Debug as well; it failed to compile before that fix.


The no-Vulkan CI environment was also reproduced locally with Docker: Ubuntu
24.04 image `sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517`,
GNU 13.3.0, and only `cmake make g++ libglm-dev` installed. Both
`/usr/include/vulkan/vulkan.h` and `/usr/bin/glslc` were asserted absent before
configuration; `CMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE` and
`CMAKE_DISABLE_FIND_PACKAGE_VulkanHeaders=TRUE` were also supplied. A fresh
Debug build with `WOWEE_ENABLE_ASAN=ON` passed all 11 then-configured tests
(the initial 10 plus ready-check state). The subsequent Windows Debug run
passed all 13 configured tests, including settings-panel layout and the
ready-check member helper. The remote GitHub Actions job has not been executed
from this workspace.


After sharing and instrumenting the vendored Lua VM, a fresh no-Vulkan Ubuntu
container run built and passed all 18 then-configured tests (including the Lua
protected-call, handler-global, legacy iteration, argument-coercion and injected
snippet regressions). The configure/build/CTest transcript is retained locally
at `build-headless-20260908/linux-sanitizer-result.txt`. Windows Debug also
passed these 18 tests using the same pinned GLM 1.0.1 installation recipe as CI.
CI explicitly sets `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1` so a UBSan
report fails the job; that stricter environment was added after the local run.
