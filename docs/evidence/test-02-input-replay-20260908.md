# TEST02 input replay evidence

Date: 2026-09-08

Source baseline: `73e63d3cc`

The opt-in unattended input trace now records key-down and key-up events when
they are actually removed from SDL's event queue. `Input::update()` merges that
replay state with hardware and mobile virtual-key state. Replay modifier state
is exposed through `SDL_GetModState()` for existing C++ and Lua consumers and is
restored when the trace scope exits. With replay disabled, ordinary pushed SDL
events retain the previous polling behavior.

Validation:

```text
cmake -S . -B build-input-ci-validation -G "Visual Studio 17 2022" -A x64
  -DWOWEE_HEADLESS_TESTS_ONLY=ON -DWOWEE_BUILD_TESTS=ON
  -DWOWEE_TEST_SDL_EVENTS=ON -DWOWEE_ENABLE_TRACY=OFF
  -DCMAKE_PREFIX_PATH=G:/WoW Projects/wowee/build-headless-20260908/glm-install
  -DSDL2_DIR=G:/WoW Projects/wowee/build-win/vcpkg_installed/x64-windows/share/sdl2
cmake --build build-input-ci-validation --target test_input_trace_sdl --config Release -j 2
ctest --test-dir build-input-ci-validation -C Release -R ^input_trace_sdl$ --output-on-failure
```

Result after the keyboard bridge: 1/1 test passed in 0.09 seconds. The target
contained 5 test cases with 78 assertions, including multi-update hold/release,
modifier transitions, focus-loss cleanup, preservation of mobile virtual keys,
scope restoration, and disabled-by-default behavior.

Follow-up source baseline `6993a86a6` extends the same opt-in bridge to SDL's
polled mouse state. A first motion uses the trace's relative delta rather than
jumping from the physical cursor; held buttons survive updates; button-event
coordinates do not masquerade as motion; focus loss releases buttons without
moving the cursor; and scope cleanup suppresses the transition back to the
physical cursor for one update. Disabled replay still reports SDL's physical
mouse state. The rebuilt target passes 110 assertions in 7 cases; CTest passes
1/1 in 0.09 seconds.

`git diff --check` passed for all implementation, registration, test, and
evidence files.
