#!/usr/bin/env python3
"""Code written for a phase that has already shipped.

    tools/reserved_code_check.py            # report, exit non-zero on a stale marker
    tools/reserved_code_check.py --list     # every marker, grouped by phase

WHY

The modern-rendering plan lets a phase build plumbing beyond what its own
techniques need, when doing it now is cheaper than doing it twice and it does
not change the frame (docs/plan-modern-rendering.md Â§7.3). Phase 01's per-frame
block gets the cascade matrices and the SH9 coefficients phase 11 fills;
phase 03's pre-pass writes the velocity target phase 08 fills.

That is a good trade exactly once. Left alone it becomes a field nobody
remembers the reason for, bound to a zero-filled buffer, in a struct seventeen
shaders mirror. So every piece of it is marked with the phase that consumes it,
and the phase that consumes it deletes the marker in the same commit:

    // RESERVED(phase-11, L5-sky-probes): SH9 ambient slots appended now so
    // PerFrame moves once. Zero-filled; no shader reads them.
    glm::vec4 skySH[7];

WHAT IT LOOKS FOR

Every RESERVED marker in the tree, and the phase each one names, against the
"Shipped through" line in docs/modern-rendering/README.md. A marker naming a
phase at or below that line is code whose consumer has already shipped without
consuming it - either the phase forgot, or the speculation was wrong and the
code should go.

Also fails a marker that does not parse. The format is fixed so that this check
and a person reading the file agree about what it says:

    RESERVED(phase-<N>, <technique-id>): <why now>

<N> is a session number from modern-rendering/README.md, optionally with a
letter for a follow-up phase inserted between two (phase-01b). The technique id
is the one from plan Â§4 - L1-csm, S1-gtao - and the reason is a sentence, not a
word.

WHAT IT CANNOT SEE

Reserved code with no marker on it. Nothing can.
"""
import argparse
import os
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
README = ROOT / "docs" / "modern-rendering" / "README.md"

# Directories that are not this project's source.
SKIP_DIRS = {".git", "build", "build-clang", "extern", "third_party", "node_modules",
             "site", ".build-docker", "Data"}
SKIP_PREFIXES = ("build",)

SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".cc", ".glsl", ".py", ".txt",
                   ".cmake", ".lua", ".sh", ".java", ".kt"}

MARKER = re.compile(r"RESERVED\(([^)]*)\)\s*:?(.*)")
WELL_FORMED = re.compile(r"^phase-(\d{1,2})([a-z]?),\s*([A-Za-z0-9]+[A-Za-z0-9\-]*)$")


def shipped_through():
    """The last phase that shipped, as a number. 0 when none have."""
    if not README.is_file():
        return None, "docs/modern-rendering/README.md is missing"
    for line in README.read_text(encoding="utf-8", errors="replace").splitlines():
        match = re.search(r"\*\*Shipped through:\s*(.+?)\.?\*\*", line)
        if not match:
            continue
        value = match.group(1).strip().lower()
        if value in ("none", "nothing"):
            return 0, None
        number = re.search(r"(\d{1,2})", value)
        if number:
            return int(number.group(1)), None
        return None, "cannot read a phase number from %r" % value
    return None, "no 'Shipped through' line in docs/modern-rendering/README.md"


def source_files():
    # os.walk with the directory list pruned in place, not rglob: a build tree
    # and an extracted Data directory hold tens of thousands of files each, and
    # rglob visits every one of them before this gets to say no. That was the
    # difference between half a minute and an instant.
    self_name = pathlib.Path(__file__).name
    for directory, subdirs, files in os.walk(ROOT):
        subdirs[:] = [d for d in subdirs
                      if d not in SKIP_DIRS
                      and not d.startswith(SKIP_PREFIXES)
                      and not d.startswith(".")]
        for name in files:
            if pathlib.Path(name).suffix not in SOURCE_SUFFIXES:
                continue
            # This file spells the format out twice, in the docstring and in
            # the message it prints. Neither is reserved code.
            if name == self_name and pathlib.Path(directory) == ROOT / "tools":
                continue
            yield pathlib.Path(directory) / name


def markers():
    """(path, line number, phase number, suffix, technique, why, raw) for each."""
    found = []
    for path in source_files():
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if "RESERVED(" not in text:
            continue
        for number, line in enumerate(text.splitlines(), start=1):
            match = MARKER.search(line)
            if not match:
                continue
            inside, why = match.group(1).strip(), match.group(2).strip()
            shape = WELL_FORMED.match(inside)
            if not shape:
                found.append((path, number, None, "", inside, why, line.strip()))
                continue
            found.append((path, number, int(shape.group(1)), shape.group(2),
                          shape.group(3), why, line.strip()))
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true",
                        help="print every marker rather than only the stale ones")
    args = parser.parse_args()

    shipped, problem = shipped_through()
    if problem:
        print(problem)
        return 1

    found = markers()
    malformed = [m for m in found if m[2] is None]
    # A marker on the phase that just shipped is stale unless it carries a
    # letter suffix, which is a follow-up phase inserted after it: phase 01
    # shipping does not consume something marked for phase-01b.
    stale = [m for m in found
             if m[2] is not None and m[2] <= shipped and not (m[2] == shipped and m[3])]
    thin = [m for m in found if m[2] is not None and len(m[5]) < 12]

    if args.list:
        for path, line, phase, suffix, technique, why, _ in sorted(
                found, key=lambda m: (m[2] if m[2] is not None else 99, str(m[0]))):
            where = "%s:%d" % (path.relative_to(ROOT).as_posix(), line)
            if phase is None:
                print("  ??      %-50s %s" % (where, technique))
            else:
                print("  %2d%-4s  %-50s %s - %s" % (phase, suffix, where, technique, why))

    print("shipped through phase %d" % shipped)
    print("%d reserved marker(s)" % len(found))
    print("%d marker(s) for a phase that has already shipped" % len(stale))
    print("%d malformed marker(s)" % len(malformed))
    print("%d marker(s) with no reason worth reading" % len(thin))

    for path, line, _, _, inside, _, raw in malformed:
        print("  MALFORMED %s:%d  %s" % (path.relative_to(ROOT).as_posix(), line, raw))
        print("            expected RESERVED(phase-<N>, <technique-id>): <why now>")
    for path, line, phase, suffix, technique, why, _ in stale:
        print("  STALE     %s:%d  phase-%d%s %s - %s"
              % (path.relative_to(ROOT).as_posix(), line, phase, suffix, technique, why))
        print("            phase %d has shipped; consume the code or delete it" % phase)
    for path, line, phase, suffix, technique, _, raw in thin:
        print("  THIN      %s:%d  %s" % (path.relative_to(ROOT).as_posix(), line, raw))
        print("            the reason is the point of the marker; write one")

    return 1 if (malformed or stale or thin) else 0


if __name__ == "__main__":
    sys.exit(main())
