#!/usr/bin/env python3
"""Options panel controls whose CVar nothing ever reads.

Every checkbox, slider and dropdown in the options panels names a CVar in
self.cvar. The control will save that CVar and read it back happily whether or
not anything acts on it, so a setting with no reader looks exactly like a
setting that works: it remembers what you chose and changes nothing.

A CVar counts as read if any of these mention it, outside the panel definition
that declares it:

  * FrameXML asking for it directly - GetCVar/GetCVarBool/SetCVar
  * a uvarInfo entry mapping it to a global, where that global is read
  * the client itself - storedCVarValue, or the name as a string literal

Names are matched case-insensitively, because the client lowercases them.

One kind of mention is not a reader. A row in settings_schema.cpp carrying an
`unavailable` reason names, in its store, the CVar the original client's
control wrote - and the whole point of that row is that nothing here reads it.
Counting the row as a reader would silence the sweep for exactly the settings
it should keep watching, so those names are looked for everywhere except that
file, and land in the handled bucket rather than the read one.

There are two ways a dead setting can be handled and they are counted apart,
because they say different things to a player. Hidden - the frame is in
kRemovedControlsLua - takes the control off the panel and leaves a hole.
Shown with a reason - a schema row with `unavailable` - draws it greyed and
says why. Both are honest; only the second answers the player who went
looking. Keeping them separate means moving a setting from one to the other is
visible here, and means taking a control back out of kRemoved cannot quietly
turn a hidden dead setting into a live one nothing reads.

A control built in Lua rather than XML has no name this can resolve, so
greying it does not take it off the list. That under-credits by two today (the
two voice device dropdowns) and errs towards reporting a setting as dead, which
is the safe direction for a ratchet.

Run with --canary to check the sweep can still see: it plants a control naming
a CVar nothing reads and fails if that is not reported. A matcher that has gone
blind reads exactly like a clean tree.

What that canary proves is narrow, and it is worth being plain about: it shows
the sweep can still report a name that appears nowhere at all. It cannot show
the reader test is calibrated, because a test that counts too much still
reports a name it never sees. Widening what counts as a reader is checked by
the finding count, not by the canary.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PANELS = ROOT / "Data/interface/framexml"
LUA_ROOTS = [ROOT / "Data/interface"]
CPP_ROOTS = [ROOT / "src", ROOT / "include"]
#: The rows the panels are built from. Read twice: as part of the C++ corpus
#: for every other name, and parsed below for the rows that declare a setting
#: this client cannot honour.
SCHEMA_CPP = ROOT / "src/ui/settings_schema.cpp"

# The files that declare controls. A mention inside one of these does not make
# a setting live, and both ways of being clever about that were tried:
#
#   * Counting every mention took this sweep from 29 findings to 3, because
#     those files carry a table keyed by CVar name for tooltip text, so nearly
#     every setting appears in them. The canary still passed - a planted name
#     that appears nowhere cannot detect a reader test that has gone slack.
#   * Counting only the by-name asks - GetCVar("x") - left two settings reading
#     as live whose only reader greys a neighbouring control.
#
# The second is the honest measure of the wrong thing. A panel consulting
# itself to grey a sibling changes the panel, not the game, and this sweep is
# looking for controls that change nothing in the game. cameraSmoothStyle was
# the case that prompted the question and it proves the point: it is asked for
# by name in interfaceoptionspanels.lua, and until it was wired up it still did
# not move the camera by one degree.
DECL_FILES = {"interfaceoptionspanels.xml", "interfaceoptionspanels.lua",
              "videooptionspanels.xml", "videooptionspanels.lua",
              "audiooptionspanels.xml", "audiooptionspanels.lua"}

CVAR_DECL = re.compile(r'self\.cvar\s*=\s*"([A-Za-z0-9_]+)"')
FRAME_DECL = re.compile(r'<Frame\s+name="([A-Za-z0-9_]+)"')
CONTROL_DECL = re.compile(r'<(?:CheckButton|Slider|Button|Frame)\s+name="(\$parent[A-Za-z0-9_]*|[A-Za-z0-9_]+)"')
#: A name in kRemovedControlsLua - one plain string per line.
REMOVED = re.compile(r'^\s*"([A-Za-z0-9_]+)",\s*(?:\.\w+\s*=\s*)?$', re.M)
#: `function SomeFrameName_OnLoad (self)` - the frame is the part before _On.
LUA_HANDLER = re.compile(r'function\s+([A-Za-z0-9_]+?)_On[A-Za-z]+\s*\(')
UVAR_DECL = re.compile(r'self\.uvar\s*=\s*"([A-Za-z0-9_]+)"')
UVAR_ENTRY = re.compile(r'\["([A-Z0-9_]+)"\]\s*=\s*\{[^}]*cvar\s*=\s*"([A-Za-z0-9_]+)"')


def read(p):
    try:
        return p.read_text(errors="ignore")
    except OSError:
        return ""


#: A start or end tag. Attribute values may hold ">" so they are consumed as
#: quoted runs rather than scanned for the closing bracket.
TAG = re.compile(r'<(/?)([A-Za-z][\w.]*)((?:[^>"\']|"[^"]*"|\'[^\']*\')*?)(/?)>', re.S)
NAME_ATTR = re.compile(r'\bname\s*=\s*"([^"]*)"')


def _controls_in_xml(text):
    """(cvar, control name, offset) for every self.cvar in one panel file.

    A depth stack over the tags, so a cvar is attributed to the element it is
    written inside. $parent is that element, which is not the nearest preceding
    named frame: a named sibling declared just above wins that race and gives a
    name no frame answers to. The four voice sliders are the example -
    $parentSpeakerVolume inside AudioOptionsVoicePanel sits after a named
    BindingOutput sibling, so a textual scan reads
    AudioOptionsVoicePanelBindingOutputSpeakerVolume and the frame is
    AudioOptionsVoicePanelSpeakerVolume.

    Hand-rolled rather than xml.etree, which the security scan blocks and which
    this does not need: nothing here parses anything but the repository's own
    markup, and only names and nesting are wanted. Checked against the parser
    it replaced across all 140 panel files, name for name.
    """
    text = re.sub(r"<!--.*?-->", "", text, flags=re.S)
    events = []
    for m in TAG.finditer(text):
        events.append((m.start(), "tag", m))
    for m in CVAR_DECL.finditer(text):
        events.append((m.start(), "cvar", m))
    events.sort(key=lambda e: e[0])

    stack, out = [], []
    for pos, kind, m in events:
        if kind == "cvar":
            owner = next((f for f in reversed(stack) if f), None)
            out.append((m.group(1).lower(), owner, pos))
            continue
        closing, _tag, attrs, selfclose = m.groups()
        if closing:
            if stack:
                stack.pop()
            continue
        nm = NAME_ATTR.search(attrs)
        resolved = None
        if nm:
            raw = nm.group(1)
            parent = next((f for f in reversed(stack) if f), None)
            resolved = (parent + raw[len("$parent"):]) if raw.startswith("$parent") and parent \
                       else (None if raw.startswith("$parent") else raw)
        if not selfclose:
            stack.append(resolved)
    return out


def declared_controls():
    """CVar -> (file:line, control frame name or None)."""
    out = {}
    # The options panels first. A CVar can be declared on a unit frame as well
    # as on the control that sets it - targetStatusText is declared five times
    # across focusframe, targetframe and the panel - and the control the player
    # sees is the one this sweep is about. Document order alone picks whichever
    # file sorts first, which was focusframe.
    files = sorted(PANELS.glob("*.xml"),
                   key=lambda q: (q.name not in DECL_FILES, q.name))
    for q in files:
        text = read(q)
        for cvar, ctrl, pos in _controls_in_xml(text):
            if ctrl is None:
                continue
            out.setdefault(cvar, (f"{q.name}:{text.count(chr(10), 0, pos) + 1}", ctrl))

    # Controls built in Lua rather than XML. Their frame name is in the handler
    # they are declared inside - AudioOptionsSoundPanelHardwareDropDown sets its
    # cvar in AudioOptionsSoundPanelHardwareDropDown_OnLoad - so the enclosing
    # function names the control the same way $parent does in the markup.
    # Without this a device dropdown could be greyed and still read as
    # unhandled, because nothing here knew what it was called.
    for p in sorted(PANELS.glob("*.lua")):
        text = read(p)
        for m in CVAR_DECL.finditer(text):
            line = text.count("\n", 0, m.start()) + 1
            fns = list(LUA_HANDLER.finditer(text[:m.start()]))
            ctrl = fns[-1].group(1) if fns else None
            out.setdefault(m.group(1).lower(), (f"{p.name}:{line}", ctrl))
    return out


def removed_controls():
    """Frame names the client takes off its panels.

    A control on a page the client drops whole counts as removed too - the
    player cannot reach it either way, and listing its thirteen controls
    individually as well would be the same fact written twice.
    """
    text = read(ROOT / "include/addons/addon_lua_snippets.hpp")
    start = text.find("kRemovedControlsLua")
    if start == -1:
        return set(), set()
    end = text.find(")LUA", start)
    body = text[start:end]
    cut = body.find("kRemovedCategories")
    names = set(REMOVED.findall(body[:cut] if cut != -1 else body))
    pages = set(REMOVED.findall(body[cut:])) if cut != -1 else set()
    return names, pages


def uvar_map():
    """cvar (lower) -> uvar global, from uvarInfo entries."""
    text = read(PANELS / "interfaceoptionsframe.lua")
    return {c.lower(): u for u, c in UVAR_ENTRY.findall(text)}


#: One C++ string literal, with its body captured. Escapes are consumed whole
#: so a backslash-quote inside a tooltip does not end it early.
STRING_LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
CHAR_LIT = re.compile(r"'(?:[^'\\]|\\.)*'")
#: SettingDesc field positions, in declaration order - see the struct in
#: include/ui/settings_schema.hpp. Only these two are wanted: the store names
#: the CVar, and a non-empty unavailable says the client cannot honour it.
STORE_FIELD = 12
UNAVAILABLE_FIELD = 15


def _strip_comments(text):
    """C++ source with comments removed, string and char literals untouched."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text[i] == '"':
            m = STRING_LIT.match(text, i)
            if m:
                out.append(m.group(0))
                i = m.end()
                continue
        elif text[i] == "'":
            m = CHAR_LIT.match(text, i)
            if m:
                out.append(m.group(0))
                i = m.end()
                continue
        elif text.startswith("//", i):
            j = text.find('\n', i)
            i = n if j == -1 else j
            continue
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j == -1 else j + 2
            out.append(" ")
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def _schema_rows(text):
    """The body of every brace group one level inside the kSchema table."""
    start = text.find("kSchema[]")
    if start == -1:
        return []
    i = text.find("{", start)
    if i == -1:
        return []
    rows, depth, row_start = [], 0, None
    while i < len(text):
        c = text[i]
        if c == '"':
            m = STRING_LIT.match(text, i)
            if m:
                i = m.end()
                continue
        if c == "{":
            depth += 1
            if depth == 2:
                row_start = i + 1
        elif c == "}":
            depth -= 1
            if depth == 1 and row_start is not None:
                rows.append(text[row_start:i])
                row_start = None
            elif depth == 0:
                break
        i += 1
    return rows


def _fields(row):
    """One row split at its top-level commas."""
    out, buf, depth, i = [], "", 0, 0
    while i < len(row):
        c = row[i]
        if c == '"':
            m = STRING_LIT.match(row, i)
            if m:
                buf += m.group(0)
                i = m.end()
                continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            out.append(buf)
            buf = ""
            i += 1
            continue
        buf += c
        i += 1
    out.append(buf)
    return out


def _string_value(field):
    """A field's string value - adjacent literals joined, "" if it is not one."""
    return "".join(STRING_LIT.findall(field))


def unavailable_cvars():
    """CVar (lower) -> setting key, for rows that declare a reason they cannot work.

    These name a CVar in their store because that is still where a macro
    writing the setting would put the value, and the row exists to say nothing
    here reads it. The name being in this file is therefore the opposite of a
    reader, and readers() is told to skip the file for it.
    """
    text = _strip_comments(read(SCHEMA_CPP))
    rows = _schema_rows(text)
    if SCHEMA_CPP.exists() and not rows:
        # Parsing nothing looks exactly like there being nothing to find, and
        # would quietly hand every one of these names back to the corpus as a
        # reader. The table is never empty, so say so instead.
        raise SystemExit(f"{SCHEMA_CPP.name}: no schema rows parsed - "
                         "the kSchema table moved or was renamed")
    out = {}
    for row in rows:
        fields = _fields(row)
        if len(fields) <= UNAVAILABLE_FIELD:
            continue
        if not _string_value(fields[UNAVAILABLE_FIELD]):
            continue
        store = _string_value(fields[STORE_FIELD])
        if not store.startswith("cvar:"):
            continue
        out[store[len("cvar:"):].lower()] = _string_value(fields[0])
    return out


def gather(roots, suffixes):
    for root in roots:
        if not root.exists():
            continue
        for p in root.rglob("*"):
            if p.is_file() and p.suffix in suffixes:
                yield p


def readers(extra_snippets):
    """Lowercased names mentioned anywhere that is not a declaration site."""
    seen = set()
    globals_read = set()
    for p in gather(LUA_ROOTS, {".lua", ".xml"}):
        if p.name in DECL_FILES:
            continue
        text = read(p).lower()
        seen.add((p, text))
        globals_read.add((p, text))
    for p in gather(CPP_ROOTS, {".cpp", ".hpp", ".h"}):
        seen.add((p, read(p).lower()))
    for name, text in extra_snippets:
        seen.add((name, text.lower()))
    return seen


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--canary", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    controls = declared_controls()
    uvars = uvar_map()
    removed, removedPages = removed_controls()
    unavailable = unavailable_cvars()

    extra = []
    if args.canary:
        controls["woweecanarysettingnoreader"] = ("canary:0", None)

    corpus = readers(extra)

    dead = []
    hidden = []
    reasoned = []
    for cvar, (where, ctrl) in sorted(controls.items()):
        # For a name a schema row declares unavailable, that row is not a
        # reader - it is the record of there being none - so the file it is in
        # is left out of the corpus for this name only. Every other name is
        # still looked for in it, because that is where the rows this client
        # does honour say what they write.
        skip = SCHEMA_CPP if cvar in unavailable else None
        found = any(cvar in text for src, text in corpus if src != skip)
        if not found and cvar in uvars:
            g = uvars[cvar].lower()
            found = any(g in text for src, text in corpus if src != skip)
        if not found:
            # Whether the game's own control is still reachable decides this
            # first, and a schema row does not excuse one that is. The greyed
            # row and the original control are two controls for one setting,
            # and the reason the original stays hidden is that the live one
            # would be the one that lies: it takes the click, writes the CVar,
            # and reports the setting as applied. So take a name back out of
            # kRemoved and it is a finding again, reason row or not.
            offPanel = ctrl and (ctrl in removed
                                 or any(ctrl.startswith(p) for p in removedPages))
            if not offPanel:
                dead.append((cvar, where))
            elif cvar in unavailable:
                reasoned.append((cvar, where))
            else:
                hidden.append((cvar, where))

    total = len(controls)
    print(f"settings with no reader and still on a panel: {len(dead)} of {total} declared "
          f"({len(hidden) + len(reasoned)} more are dead and handled: {len(hidden)} hidden "
          f"from the panels, {len(reasoned)} shown greyed with a reason)")
    if args.verbose:
        for cvar, where in reasoned:
            print(f"  greyed  {cvar:36s} {where}  -> {unavailable[cvar]}")
        for cvar, where in hidden:
            print(f"  hidden  {cvar:36s} {where}")
    for cvar, where in dead:
        print(f"  {cvar:38s} {where}")

    if args.canary:
        if not any(c == "woweecanarysettingnoreader" for c, _ in dead):
            print("CANARY FAILED: planted dead setting was not reported")
            return 1
        print("canary ok")
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
