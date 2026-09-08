#!/usr/bin/env python3
"""Run real framexml_run CLI failure cases with isolated addon/config fixtures.

A failing stock baseline makes dependent negative cases inconclusive. A crash,
timeout, or unrelated startup failure never counts as successful error handling.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def classify(returncode: int | None, output: str, marker: str,
             baseline_ok: bool, needs_baseline: bool, expected_success: bool = False) -> str:
    if returncode is None:
        return "timeout"
    if returncode < 0 or returncode >= 0x80000000:
        return "crash"
    if any(marker in output for marker in ("Assertion failed:", "abort() has been called",
                                           "terminate called after throwing")):
        return "crash"
    if expected_success:
        if returncode != 0 or marker not in output:
            return "failed_baseline"
        if needs_baseline and not baseline_ok:
            return "inconclusive_failing_baseline"
        return "pass"
    if returncode == 0:
        return "false_success"
    if marker not in output:
        return "wrong_failure"
    if needs_baseline and not baseline_ok:
        return "inconclusive_failing_baseline"
    return "pass"


def prepare_assets(source: Path, destination: Path, manifest_text: str) -> dict:
    # Copies only files the interface loader reads and its real fonts. The
    # original art stays read-only behind the manifest; SavedVariables are new.
    for directory, suffixes in (("interface", {".lua", ".xml", ".toc"}),
                                ("misc/fonts", {".ttf"})):
        for path in (source / directory).rglob("*"):
            if path.is_file() and path.suffix.lower() in suffixes:
                target = destination / path.relative_to(source)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, target)
    destination.mkdir(parents=True, exist_ok=True)
    # Prefer a portable relative route. Windows fixtures on another drive
    # require the absolute route supported by the native manifest loader.
    declared_base = Path(json.loads(manifest_text).get("basePath", "."))
    original_base = (source / declared_base).resolve()
    try:
        route = Path(os.path.relpath(original_base, destination)).as_posix()
    except ValueError:
        route = original_base.as_posix()
    routed, replacements = re.subn(
        r'("basePath"\s*:\s*)"(?:[^"\\]|\\.)*"',
        lambda match: match.group(1) + json.dumps(route), manifest_text, count=1)
    if replacements != 1:
        raise ValueError("source manifest must declare basePath")
    manifest = destination / "manifest.json"
    manifest.write_text(routed, encoding="utf-8", newline="\n")
    return {"asset_root": str(destination), "manifest_sha256": sha256(manifest),
            "manifest_base_path": route, "saved_variables": "fresh, fixture-local"}


def run(binary: Path, assets: Path, output: Path, timeout: float) -> dict:
    output.mkdir(parents=True, exist_ok=False)
    manifest = assets / "manifest.json"
    manifest_text = manifest.read_text(encoding="utf-8")
    inputs = sorted(path for path in (assets / "interface").rglob("*")
                    if path.is_file() and path.suffix.lower() in {".lua", ".xml", ".toc"})
    report = {
        "binary": str(binary), "binary_sha256": sha256(binary),
        "source_assets": str(assets), "source_manifest_sha256": sha256(manifest),
        "interface_files": {str(path.relative_to(assets)): sha256(path) for path in inputs},
        "server": "none; production offline FrameXML runner", "default_viewport": "1920x1080",
        "fallback": "0", "process_timeout_seconds": timeout, "cases": [],
    }
    scenarios = [
        ("baseline", ["--lua:assert(true)"], "   ran", True, False),
        ("viewport_1024", ["--viewport:1024x768",
            "--lua:assert(math.abs(GetScreenWidth()-1024)<0.01, 'VIEWPORT_WIDTH'); "
            "assert(math.abs(GetScreenHeight()-768)<0.01, 'VIEWPORT_HEIGHT'); "
            "assert(math.abs(UIParent:GetWidth()-1024)<0.01, 'VIEWPORT_ROOT')"], "   ran", True, True),
        ("viewport_invalid", ["--viewport:1024x0"], "each dimension 1..16384", False, False),
        ("viewport_duplicate", ["--viewport:1024x768", "--viewport:1920x1080"],
            "requires one WIDTHxHEIGHT", False, False),
        ("hit_negative", ["--hit:-1,-2"], "hit at -1,-2", True, True),
        ("mouse_negative", ["--mouse:-1,-2,LR"], "mouse at -1,-2 holding 'LR'", True, True),
        ("hit_trailing_garbage", ["--hit:1,2junk"],
            "--hit: requires finite X,Y coordinates", False, False),
        ("hit_nonfinite", ["--hit:nan,2"],
            "--hit: requires finite X,Y coordinates", False, False),
        ("mouse_unknown_button", ["--mouse:1,2,X"],
            "--mouse: requires finite X,Y and only L, R, M buttons", False, False),
        ("missing_arguments", [], "usage: framexml_run", False, False),
        ("missing_assets", [], "asset directory does not exist", False, False),
        ("empty_expression", [""], "empty expression", False, False),
        ("empty_lua", ["--lua:   "], "empty expression", False, False),
        ("missing_script", [], "missing, unreadable or empty script", False, False),
        ("empty_script", [], "missing, unreadable or empty script", False, False),
        ("syntax_error", [], "near", False, True),
        ("assertion", [], "MATRIX_ASSERTION", False, True),
        ("throw", [], "MATRIX_THROW", False, True),
        ("protected_throw", [], "MATRIX_PROTECTED", False, True),
        ("lua_timeout", [], "runaway script aborted", False, True),
        ("unknown_option", ["--matrix-unknown"], "unknown runner option", False, True),
        ("invalid_ticks", ["--tick:not-a-number"], "requires an integer", False, True),
        ("missing_manifest", ["--lua:assert(true)"], "== assets: none", False, True),
        ("missing_framexml", ["--lua:assert(true)"], "result=failed", False, True),
    ]
    scripts = {
        "empty_script": " \n\t",
        "syntax_error": "local broken = )\n",
        "assertion": "assert(false, 'MATRIX_ASSERTION')\n",
        "throw": "error('MATRIX_THROW')\n",
        "protected_throw": "seterrorhandler(function() end)\nsecurecall(function() error('MATRIX_PROTECTED') end)\n",
        "lua_timeout": "while true do end\n",
    }
    baseline_ok = False
    for name, arguments, marker, success, needs_baseline in scenarios:
        case_root = output / name
        case_root.mkdir()
        config = case_root / "config"
        config.mkdir()
        fixture = case_root / "assets"
        identity = prepare_assets(assets, fixture, manifest_text)
        arguments = list(arguments)
        if name in scripts:
            script = case_root / "scenario.lua"
            script.write_text(scripts[name], encoding="utf-8", newline="\n")
            arguments = ["--script:" + str(script)]
            identity["script_sha256"] = sha256(script)
        if name == "missing_script":
            arguments = ["--script:" + str(case_root / "absent.lua")]
        if name == "missing_manifest":
            (fixture / "manifest.json").unlink()
        if name == "missing_framexml":
            # Do not delete source files. Only the copied fixture's manifest.
            for toc in (fixture / "interface").rglob("*"):
                if toc.is_file() and toc.name.lower() == "framexml.toc":
                    toc.unlink()
        command = [str(binary), str(fixture), *arguments]
        if name == "missing_arguments":
            command = [str(binary)]
        elif name == "missing_assets":
            command = [str(binary), str(case_root / "absent-assets")]
        env = os.environ.copy()
        env.update(WOWEE_CONFIG_ROOT=str(config), WOWEE_LOG_FILE=str(case_root / "engine.log"),
                   WOWEE_LUA_API_FALLBACK="0", WOWEE_LOAD_FRAMEXML="1")
        # DLL discovery remains relative to the executable; CWD is isolated so
        # incidental relative writes cannot touch the user's workspace.
        env["PATH"] = str(binary.parent) + os.pathsep + env.get("PATH", "")
        stdout = case_root / "stdout.log"
        started = time.monotonic()
        with stdout.open("wb") as stream:
            process = subprocess.Popen(command, cwd=case_root, env=env, stdout=stream,
                                       stderr=subprocess.STDOUT,
                                       creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                code = None
        captured = stdout.read_text(encoding="utf-8", errors="replace")
        status = classify(code, captured, marker, baseline_ok, needs_baseline, success)
        if name == "baseline":
            baseline_ok = status == "pass"
        result = {"name": name, "status": status, "exit_code": code,
                  "elapsed_seconds": round(time.monotonic() - started, 3),
                  "expected_marker": marker, "marker_present": marker in captured,
                  "command": command, "config_root": str(config),
                  "stdout": str(stdout), "identity": identity}
        report["cases"].append(result)
        # Persist after every process, so a later interruption retains evidence.
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {status} (exit={code})", flush=True)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--timeout", type=float, default=120.0)
    args = parser.parse_args()
    if not args.binary.is_file() or not (args.assets / "manifest.json").is_file():
        parser.error("binary and extracted asset manifest must exist")
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    report = run(args.binary.resolve(), args.assets.resolve(), args.output.resolve(), args.timeout)
    return 0 if all(case["status"] == "pass" for case in report["cases"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
