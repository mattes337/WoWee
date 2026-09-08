# TEST-02 mouse replay Linux overlay validation

Date: 2026-09-08

This is a focused follow-up result, separate from the frozen combined baseline.
The immutable source base was
`C:/wowee-headless-baseline-915c8752f/source`, captured at `915c8752f` and
mounted read-only as `/src`. The tested overlay was commit `0b437c9dd` and
mounted these files individually, also read-only:

| Overlay file | SHA-256 |
|---|---|
| `include/core/input.hpp` | `45a325f5e32e06e7c6e237fce0606ef3be44bab9916a0729cdd2d6a63a02a7c7` |
| `src/core/input.cpp` | `9bfe3a6f8a12a2906c88d4c7cd012e8d877aff0e6a1bcc5facb3f5d12c4c28bc` |
| `tests/test_input_trace_sdl.cpp` | `0488e6f8c70dce3cc632fba5ac5112b1f4c8be798360b31379bb2d99c37b2a92` |

The commit's documentation-only fourth file was not needed to build the
overlay. CMake build products lived under container-local `/tmp/headless`; no
file in the frozen snapshot was writable.

The container used `ubuntu:24.04` with GNU C++ 13.3.0. Before and after
installing `libsdl2-dev` with `--no-install-recommends`, both
`/usr/include/vulkan/vulkan.h` and `/usr/bin/glslc` were absent. The SDL package
did install the `libvulkan1` runtime library; no Vulkan SDK headers or shader
compiler were available. Configuration used:

```text
cmake -S /src -B /tmp/headless
  -DWOWEE_HEADLESS_TESTS_ONLY=ON
  -DWOWEE_ENABLE_ASAN=ON
  -DWOWEE_TEST_SDL_EVENTS=ON
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_DISABLE_FIND_PACKAGE_Vulkan=TRUE
  -DCMAKE_DISABLE_FIND_PACKAGE_VulkanHeaders=TRUE
cmake --build /tmp/headless --target test_input_trace_sdl -j2
```

CTest and direct execution both used:

```text
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
```

Result: the focused target built successfully; CTest passed 1/1 in 0.05
seconds; direct execution passed 110 assertions in 7 cases. No ASan leak or
memory report and no UBSan report was emitted. No client, GPU scenario, or
server was run.
