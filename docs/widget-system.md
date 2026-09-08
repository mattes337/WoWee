# The widget system

Source review: 2026-09-08, fork revision `51277f5f3` (widget implementation
inherited from `3f92198677`). Links below identify current source; historical
load timings and interface handover notes are not fresh runtime acceptance.
See [the capability ledger](capability-ledger.md) and [fork roadmap](fork-roadmap.md)
for remaining validation gates.

The addon API used to answer without doing anything. `CreateFrame` returned a
table, events dispatched to it, and `CreateTexture` handed back an object whose
every method was a no-op - so an addon could be written, loaded and run without
putting a pixel on the screen.

There is now a real retained widget tree behind it. The same tree is what
FrameXML targets, because FrameXML is only Lua and XML over a widget system, so
building it once serves both goals: addons that draw, and a route to running the
original interface rather than imitating it.

## Shape

| Piece | Where | Notes |
|---|---|---|
| Widget tree, anchors, draw order, hit testing | `src/ui/widget_tree.cpp` | No Vulkan or ImGui, so the layout rules are testable without a device |
| Drawing, texture cache, backdrops, status bars | `src/ui/widget_renderer.cpp` | Reads `Interface\` art through the existing asset path |
| XML reader | `src/ui/xml_parser.cpp` | Enough for FrameXML: CDATA, comments, both quote styles |
| XML to Lua | `src/ui/framexml_emitter.cpp` | Emits the calls a script would make |
| Lua bindings | `src/addons/lua_engine.cpp` | Frames and regions are Lua tables carrying a `__wid` handle |

Coordinates follow WoW throughout - origin bottom-left, y upward - and flip once
at the point of drawing, so every anchor rule reads the way Blizzard documents
it rather than mirrored.

Anchors are constraints, not positions. An anchor says "this fraction of my rect
sits at that point", so one anchor plus a size places a frame and two opposing
anchors give the size as well. That is what `SetAllPoints` relies on, and how
most of FrameXML sizes its backgrounds without ever stating a size.

## Why XML becomes Lua

The alternative was to build widgets from C++ while walking the XML, which would
have meant a second implementation of everything `CreateFrame` already does -
parenting, naming, templates, script binding - kept in step with the first by
hand. Emitting Lua means XML frames and hand-written frames travel one path, a
template declared in XML is usable from a script without translation, and the
emitter's output is a string a test can read without a Lua state.

## Environment switches

FrameXML is on by default. API fallback follows that setting unless explicitly
overridden. `WOWEE_LUA_API_FALLBACK=0` takes precedence even when FrameXML is on;
see `LuaEngine::installMissingApiFallback` in
[`lua_engine.cpp`](../src/addons/lua_engine.cpp) for the environment parsing.

### `WOWEE_LUA_API_FALLBACK=1`

Unknown globals answer with a no-op instead of erroring, and every name asked
for is logged once and listed at shutdown.

This is how a large body of Lua gets brought up: rather than guessing which of
the missing functions matter, run it and collect the ones it actually reaches.

It has a real cost. Code that checks whether a function exists before using it -
which addons do constantly - sees everything as present and takes branches meant
for a different client. Names in `SCREAMING_SNAKE_CASE` are treated as constants
and still come back nil, because handing a function to something expecting a
number turns a missing value into a confusing type error further away.

### `WOWEE_LOAD_FRAMEXML=1`

Loads the original interface from `Interface/FrameXML/FrameXML.toc`, in the
order that manifest states, before user addons. It also enables API fallback
unless `WOWEE_LUA_API_FALLBACK=0` explicitly disables it. A fresh fallback-off
load against pinned stock data remains an EVAL-01 acceptance gate.

Every file that fails is listed together at the end of the load, with the reason
carried up from whichever include or referenced script actually broke, and each
error carries the Lua call stack that reached it.

An earlier, unpinned session reported this load result. It is retained as
historical evidence only; neither the timing nor file count has been rerun
for the current fork:

    FrameXML: 13 Lua files and 126 XML files loaded, 0 failed in 377ms

Loading files does not establish behavior: state, return shapes, events and
rendered output require individual scenarios. Do not infer current API
coverage from this historical loader result.

### Whether an event actually arrives

    tools/addon_events.sh              every event this client fires
    tools/addon_events.sh ACTIONBAR    only those matching

FrameXML's frames update on events, so replacing one of the client's own
elements means knowing which of them arrive. Four different call styles
dispatch them - fireEvent, fireAddonEvent, an emit on a pending queue, and the
callback invoked directly - and grepping for one under-reports the rest badly:
the same question answered 6, 52, 73 and 147 depending on which was searched.
Ask the script.

### Working out what is still missing

    tools/framexml_api_gap.py <path to Interface/FrameXML>

reported 49 unresolved names at 62 call sites in the locally extracted
interface on 2026-09-08: 5,296 distinct called names, 3,797 detected interface
definitions and 1,507 detected provided names. These categories overlap.
The [ledger](capability-ledger.json) records exact inputs, source locations
and dispositions; this is a regex candidate list, not a completeness measure
or a certified stock-interface inventory.

The measurement that matters is a run with the fallback off:

    WOWEE_LUA_API_FALLBACK=0 WOWEE_LOAD_FRAMEXML=1 ./wowee

With fallback on, a missing name can be hidden by a default answer. With it
off, reached missing calls can surface as Lua errors. Save the error output
and any missing-API report; a first error can stop execution before later
gaps are reached, so neither report is an exhaustive API inventory.

Two tools check the front half of the pipeline:
`tools/framexml_compile_check.cpp` asks Lua whether generated files compile,
and the emitter has unit tests in
`tests/test_framexml.cpp` covering the XML features that were silently absent -
template inheritance, `parentKey`, `id`, `<ScrollChild>`, button art, handler
argument names, and `$parent` through unnamed frames.

## Replacing one element at a time

The source routes interface ownership through FrameXML.
This does not certify all panels or their actions. `WOWEE_FRAMEXML_UI`, which
named elements one at a time, has
been removed along with the thing its other setting selected - the client's own
version of nearly every element has been deleted, so `=none` stopped meaning
"use this client's interface" and started meaning "draw nothing".

What survives in `framexml_takeover.hpp` is the accounting: the frames each
element stands or falls on, the handful of places this client still draws into
a frame FrameXML owns, and the net that hands an element back when FrameXML's
version of it was never built.

Two things had to happen for an element to change hands, and only the first is
obvious. They are what the notes in that file are about.

**The client stops drawing its own.** One `frameXmlOwns` check where it used to
draw. The keybinding has to follow: each panel polls its own key from inside
its own draw, so a panel that is no longer drawn never sees the key, and
handing one over made it unopenable until the key was routed to FrameXML's own
toggle instead.

**Anything the client renders itself needs handing over explicitly.** This is
the part that is easy to miss, because the frame appears and looks right and
is empty. FrameXML's frames are frames: the picture inside them is this
client's, and it has to be told where to go.

| what | how it is handed over |
|---|---|
| unit portrait | an offscreen character pass, given to the widget as a texture |
| minimap | a Vulkan pass of its own - told the frame's rect, since it cannot be sampled |
| world map | an ImGui window - told the rect, and to drop its own title bar |
| paperdoll model | a second offscreen pass, framed to the whole figure |

A frame carrying one of these draws it beneath its own regions, so the art
around it lands on top. The rect is a frame behind, because those passes have
already run by the time the tree lays out - which for a frame that does not
move is not visible.

## Known gaps

- Fonts are loaded in [`ui_manager.cpp`](../src/ui/ui_manager.cpp) and drawn
  with an explicit size in [`widget_renderer.cpp`](../src/ui/widget_renderer.cpp).
  Bundled ImGui is 1.92.6 WIP and supports baked data at multiple sizes; the
  old claim that every face is only a scaled fixed-size atlas is obsolete.
  Outlines still use offset glyph copies, and
  [`interfaceTextWidth`](../src/ui/interface_fonts.cpp) estimates width from
  character count when no font is available during startup. PORT-04 requires
  visual/metric comparisons before choosing a replacement.
- `EditBox` has selection via `lua_EditBox_HighlightText`, caret and keyboard
  selection handling, and SDL clipboard copy/cut/paste in
  [`lua_engine.cpp`](../src/addons/lua_engine.cpp). The previous absent-selection
  and absent-clipboard claims were incorrect. Source presence does not
  establish complete Unicode, focus or overflow behavior; input acceptance
  remains under PORT-06 and TEST-06.
- Widget textures remain cached in `WidgetRenderer::textures_`; the source
  invalidates the cache when the context texture generation changes. No
  bounded per-texture LRU policy was established by this review. PORT-11
  requires measured budgets and GPU-safe lifetime checks; a few hundred
  entries is not a demonstrated upper bound.
- The widget method set in `lua_engine.cpp` is enumerated rather than derived.
  A method outside it answers nil instead of doing nothing, which for an addon
  is an error rather than a shrug. Every such name is recorded once as
  `widget:Name`, so the gap shows up in the shutdown report rather than as a
  mystery. Adding a name to the no-op set does not implement its contract;
  verify the return value, state changes and events before closing a gap.
- Blend modes are honoured only far enough to tell "added" apart from "drawn
  over". `alphaMode="ADD"` art carries no alpha channel of its own - it is a
  glow on black - so it is uploaded as a second copy of the image with its
  alpha taken from brightness, which over a dark scene lands close to where
  adding would. `MOD` and `ALPHAKEY` are still drawn as ordinary blending. One
  ImGui draw list has one blend state, so anything better means a second
  pipeline.

## Diagnostics

Every switch below is read once, from the environment, and costs nothing when
unset.

- `WOWEE_LOAD_FRAMEXML=0` turns Blizzard's interface off. It is loaded by
  default and it is the interface - there is no longer a second one behind it,
  so a run with this set has a world, a chat log in the terminal and no frames.
- `WOWEE_FRAMEXML_EMIT_DIR=/tmp/emit` writes the Lua each XML file became, one
  file per source file. A nil global or a frame in the wrong place is nearly
  always answered by one grep through this.
- `WOWEE_WIDGET_DUMP=1..5` reports what the renderer believes: 1 lists what was
  drawn, 2 every named widget whether drawn or not, 3 outlines them on screen,
  4 fills them solid, 5 also draws ImGui's own font atlas through the same call
  - which separates "AddImage does not work here" from "these textures are bad".
- `WOWEE_LUA_API_FALLBACK=0` turns off the stub that answers unknown globals,
  exposing reached missing calls as Lua errors; it does not establish that
  every panel or code path was exercised.
- `WOWEE_EVENT_TRACE=UNIT_HEALTH,UNIT_MANA` reports each of those events and how
  many frames received it. An event that never arrives and an event nobody
  listens for look identical from outside - the frame simply does not change -
  and they need opposite fixes.

With any element handed over, a check runs once the tree has settled and
reports the frames that element stands or falls on: built or not, shown or
hidden, its rect, whether it landed off screen, whether its art reached the
GPU, and for a status bar the value and range it was given. It also names any
visible widget that landed outside the display, whoever owns it - a frame in
the wrong place is only findable by name if you can guess the name, and the
thing that looks wrong is rarely the thing you would have thought to check.

The missing-API report at shutdown separates three things that are not the
same, and writes the full list to `missing_api.txt` beside the log:

- names still undefined, which are candidates requiring caller review;
- names read before the file defining them had loaded, which is normal;
- names built from an existing frame's, which are parts that frame may or may
  not have. FrameXML asks for these constantly and guards them properly.

The third category was 183 of 222 on one session. Reading the report without
that split says close to the opposite of the truth.
