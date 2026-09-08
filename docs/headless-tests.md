# Headless tests

`WOWEE_HEADLESS_TESTS_ONLY=ON` builds the real Catch2 packet, bit-packet,
spline interpolation/body/facing, widget layout, text-edit, escape-action and
XML parser/emitter/takeover and settings-panel layout tests. These same targets remain in normal client
builds; their shared definition is `cmake/HeadlessTests.cmake`.

Only a C++20 toolchain, CMake 3.15+ and GLM are required. Catch2 is vendored.
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
The common logger object and Catch2 library are instrumented as well as tests.
MSVC sanitizer support is unchanged and this option does not enable it there.

The independent `Headless tests` CI workflow uses an Ubuntu 24.04 container with
only compiler/CMake/GLM packages. It checks that Vulkan headers and glslc are
absent, disables Vulkan package discovery, builds with sanitizers, and runs every
configured test. The existing `Build` workflow retains graphics dependencies.
`ctest -L headless` selects this subset in a full build as well.

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
