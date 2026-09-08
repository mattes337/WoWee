# PORT-01 / TEST-02 bounded SDL input trace

Set `WOWEE_TEST_INPUT_TRACE` to a JSON file to queue deterministic, update-indexed
events before Application's existing SDL_PollEvent loop. The default application
does not load or queue a trace. `tests/fixtures/offline_input_trace.json` is a
small offline/login example; it never submits credentials or contacts a server.

The schema requires `version: 1`, `stop_after_updates` (1–1,000,000) and a nonempty
`events` array (at most 10,000). Events must be ordered by `after_updates`; the
value identifies how many update/render iterations have completed before the
event is queued. Equal values preserve file order. Every event must precede the
stop count. This is not elapsed network time or a presented-frame boundary.

| Type | Required fields beyond type/after_updates | Optional fields |
|---|---|---|
| mouse_move | x, y window coordinates | dx, dy, buttons (SDL button mask) |
| mouse_down / mouse_up | x, y, button (SDL index 1–5) | — |
| mouse_wheel | x, y integer wheel steps | — |
| key_down / key_up | keycode, scancode (SDL numeric values) | modifiers (SDL mask) |
| text | text (1–31 UTF-8 bytes, no NUL) | — |

The trace derives the existing unattended update limit and exits through the
normal queued SDL_QUIT path. An explicitly set WOWEE_TEST_MAX_UPDATES must match
the trace stop count. Missing/invalid/oversized traces, unknown fields/types,
invalid ordering, rejected/filtered queues and incomplete runs throw and fail
the process. Logs report event counts and completion; parser errors never echo
the JSON or text payload. Trace files themselves contain their supplied text,
so use non-secret fixture text.

## Verification

The pure parser/scheduler regression and real SDL event-queue fixture both pass
with MSVC Debug and fresh Ubuntu GNU Debug ASan + strict UBSan. See
`docs/headless-tests.md` for snapshot identity and exact suite counts. The SDL fixture initializes only SDL_INIT_EVENTS: no Vulkan,
window, original game data, or server is used. It inspects actual polled SDL
events, including position, relative motion, button/modifier state fields,
keycode/scancode, wheel and text values, and forces SDL event filtering to
verify a rejected queue cannot pass.

```powershell
cmake -S . -B build-headless-20260908 -DWOWEE_TEST_SDL_EVENTS=ON -DSDL2_DIR="G:/WoW Projects/wowee/build-win3/vcpkg_installed/x64-windows/share/sdl2"
cmake --build build-headless-20260908 --config Debug --target test_input_trace test_input_trace_sdl
ctest --test-dir build-headless-20260908 -C Debug -R '^input_trace(_sdl)?$' --output-on-failure
```

## Limits

This proves SDL queue order and conversion and supplies the normal event loop.
SDL_PushEvent does not update the OS keyboard/mouse state read by
`Input::update` in `src/core/input.cpp`. Thus this is not verified held movement,
OS pointer position, drag behavior, focus, click outcome or text-field content.
The application still uses its real clock; only event scheduling is indexed by
completed iterations. A live application trace smoke remains to be executed.
TEST-02 remains open for polled-state integration and actual UI outcome
assertions. TEST-04 capture acknowledgement and authoritative gameplay waits
remain separate. No screenshot event or game/server replacement was added.

## ImGui event-path audit

The vendored SDL backend accepts matching-window motion, buttons, keys and text
through `ImGui_ImplSDL2_ProcessEvent` (`extern/imgui/backends/imgui_impl_sdl2.cpp`).
Button events do not set pointer position from their x/y fields. Queue a
`mouse_move` immediately before `mouse_down` at the same `after_updates`, then
queue release on a later update. This also sets the backend's held-button bit
before NewFrame, preventing its focused-window global-position fallback
(`UpdateMouseData`, condition `MouseButtonsDown == 0`) from replacing that motion.
This uses the real existing event path, not emulated OS polling state.

Allow startup focus/leave events to settle before clicking. A pending window
leave may reset position when no buttons are held. ImGui's input trickling can
defer text after a mouse change, and focus loss clears keys/mouse
(`extern/imgui/imgui.cpp`, `UpdateInputEvents`). Send text on later updates after
field focus is established, and verify the actual UI result. The queue fixture
alone does not establish field focus or contents. Native focus/hover changes
can still interfere with unattended traces.

`Window::initialize` creates an `SDL_WINDOW_SHOWN` window. Windows
`CREATE_NO_WINDOW` hides a console, not this SDL window, so it cannot establish
a hidden-window input guarantee. No application or renderer behavior was changed
for this audit, and the OS-state limitation above remains open.
