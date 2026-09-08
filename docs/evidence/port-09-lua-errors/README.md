# PORT-09 bounded Lua error API implementation

The inspected Rust donor protects securecall and retains its multiple return
values. Its debugstack is also an empty stub; stack inspection here is an
independent implementation using Lua 5.1's C debug API, without exposing the
unsafe debug library. `provenance.json` and the source excerpt identify the
current donor content and source selection. The sibling tree was not modified.

WoWee now registers the four error APIs before its interface bootstrap:

- `debugstack` reports source, line and available function names, with start and
  top/bottom count limits.
- `securecall` accepts functions and raw global names, retains nil argument and
  result positions, and contains exceptions. An error returns zero values.
- The configured `geterrorhandler` receives the original error object before
  stack unwinding. Throws or nested securecall failures in that handler are
  contained; subsequent errors can still be reported. The reporting guard is
  restored and the configured handler is not silently replaced.
- `seterrorhandler` requires a function, and callers can save/restore the handler.

`test_lua_error_api` executes the production header with the vendored Lua VM.
It exercises nested named callbacks, deliberate throws, handler reentry/throws,
exact nil/multiple return shapes, error object identity, handler restoration,
stack start/count limits, and ordinary unprotected failures remaining test
failures. The previous direct securecall would propagate the deliberate throw;
the previous debugstack would fail the nonempty source/name assertions.

```powershell
cmake --build build-headless-20260908 --config Debug --target test_lua_error_api
ctest --test-dir build-headless-20260908 -C Debug -R '^lua_error_api$' --output-on-failure
```

MSVC Debug: six test cases passed (14 C++ assertions, with the semantic
assertions executed inside Lua). This is not full taint tracking (BOTH-01),
byte-identical stock stack formatting, or rendered error-dialog acceptance.
The full client build and fallback-off live addon validation remain separate.

A follow-up observer reports protected errors to LuaEngine's existing error
callback independently of an addon's chosen error handler. This preserves
securecall containment while preventing a caught assertion from producing a
green harness run. The observer regression checks two caught errors with two
different addon handlers and verifies a successful call still returns normally.
