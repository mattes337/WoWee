#!/usr/bin/env python3
"""Generate a reproducible static ledger; this does not execute the client.

Run from any directory, passing --framexml for the exact extracted interface.
The source digest covers scanned files, not asset authenticity or a server.
"""
import argparse
import contextlib
import hashlib
import io
import json
from pathlib import Path
import re
import runpy
import sys

import framexml_provides as provides
import validate_opcode_maps as opcodes

ROOT = Path(__file__).resolve().parent.parent


def locations(paths, pattern, root):
    result = {}
    for path in sorted(paths):
        for line, text in enumerate(path.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            for match in pattern.finditer(text):
                result.setdefault(match.group(1), []).append(f"{path.relative_to(root).as_posix()}:{line}")
    return result


def api_scan(framexml):
    # Reuse the existing detector so the appendix and command-line check agree.
    argv = sys.argv
    try:
        sys.argv = ["framexml_api_gap.py", str(framexml)]
        with contextlib.redirect_stdout(io.StringIO()):
            data = runpy.run_path(str(ROOT / "tools/framexml_api_gap.py"))
        return data
    finally:
        sys.argv = argv


def generate(framexml):
    framexml = framexml.resolve()
    interface = framexml.parent
    scan = api_scan(framexml)
    reviewed = json.loads((ROOT / "docs/capability-dispositions.json").read_text(encoding="utf-8"))
    aliases = opcodes.read_alias_data(ROOT / "Data/opcodes/aliases.json")
    refs = locations(list(ROOT.glob("src/**/*.cpp")) + list(ROOT.glob("include/**/*.hpp")),
                     opcodes.RE_CODE_REF, ROOT)
    maps = {p.parent.name: {opcodes.canonicalize(n, aliases) for n in opcodes.load_expansion_names(p)}
            for p in opcodes.iter_expansion_files(ROOT / "Data/expansions")}
    missing = sorted(n for n in opcodes.collect_code_refs(ROOT)
                     if opcodes.canonicalize(n, aliases) not in maps["wotlk"])
    warning_rows = []
    for name in missing:
        row = {"name": name, "evidence": refs[name],
               "mapped_profiles": sorted(p for p, names in maps.items()
                                         if opcodes.canonicalize(name, aliases) in names)}
        row.update(reviewed["opcodes"].get(name, {"disposition": "unreviewed", "owner": "EVAL-05"}))
        warning_rows.append(row)
    lua_files = [Path(p) for p in scan["_lua"]]
    call_locations = locations(lua_files, re.compile(r"(?<![\w.:])([A-Z][A-Za-z0-9_]{2,})\s*\("), interface)
    api_rows = []
    current = dict(scan["missing"])
    for name in sorted(set(current) | set(reviewed["apis"])):
        row = {"name": name, "candidate_calls": current.get(name, 0),
               "evidence": call_locations.get(name, [])}
        row.update(reviewed["apis"].get(name, {"disposition": "unreviewed", "owner": "API-01"}))
        api_rows.append(row)
    input_paths = set(lua_files) | set(ROOT.glob("src/**/*.cpp")) | set(ROOT.glob("include/**/*.hpp"))
    input_paths |= set(ROOT.glob("tools/*.py")) | set(ROOT.glob("Data/expansions/*/opcodes.json"))
    input_paths |= set(ROOT.glob("Data/opcodes/*.json")) | set(ROOT.glob("tests/*CMake*.txt"))
    input_paths.add(ROOT / "docs/capability-dispositions.json")
    for path in (framexml / "FrameXML.toc", ROOT / "Data/extracted/manifest.json"):
        if path.exists():
            input_paths.add(path)
    digest = hashlib.sha256()
    for path in sorted(input_paths):
        digest.update(path.relative_to(ROOT).as_posix().encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return {
        "schema_version": 1,
        "context": {"client_target": "WotLK 3.3.5a / 12340", "server_revision": None,
                    "scenario": "static source scan", "result": "not runtime verified",
                    "interface_path": framexml.relative_to(ROOT).as_posix(),
                    "stock_provenance": "unverified", "input_sha256": digest.hexdigest(),
                    "limitations": "Regex detections overlap and include defaults, comments and unreachable code. No coverage percentage or functional completion is implied."},
        "wotlk_opcode_warnings": warning_rows,
        "api_candidates": api_rows,
        "inventory": {"registered_global_candidates": sorted(provides.globals_provided()),
                      "widget_method_candidates": sorted(provides.widget_methods_provided()),
                      "counting_defaults": sorted(provides.counting_table()),
                      "noop_widget_candidates": sorted(provides.noop_widget_methods()),
                      "opcode_reference_candidates": refs,
                      "test_manifests": [p.relative_to(ROOT).as_posix() for p in sorted(ROOT.glob("tests/*CMake*.txt"))]},
        "uncollected": {"runtime_missing_globals": "Requires clean fallback-off session (API-01)",
                        "cvar_behavior": "Requires CVar scenario inventory (BASE-02)",
                        "dispatch_skip_behavior": "References collected; handler classification requires review (BASE-02)",
                        "configured_tests": "Use configured CTest manifest; source presence is not execution (TEST-10)"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--framexml", type=Path, default=ROOT / "Data/extracted/interface/FrameXML")
    parser.add_argument("--output", type=Path, default=ROOT / "docs/capability-ledger.json")
    parser.add_argument("--check", action="store_true", help="Fail if the saved ledger differs from current inputs.")
    args = parser.parse_args()
    result = generate(args.framexml)
    rendered = json.dumps(result, indent=2, ensure_ascii=True) + "\n"
    if args.check:
        if not args.output.exists() or args.output.read_text(encoding="utf-8") != rendered:
            print("Capability ledger is stale; regenerate it with the same interface inputs.")
            return 1
    else:
        args.output.write_text(rendered, encoding="utf-8")
    print(f"{len(result['wotlk_opcode_warnings'])} opcode warnings; {len(result['api_candidates'])} API candidate dispositions; static evidence only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
