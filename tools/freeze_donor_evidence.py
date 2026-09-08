#!/usr/bin/env python3
"""Freeze explicitly selected donor files without modifying the donor checkout.

Example: python tools/freeze_donor_evidence.py --donor ../wow-client
  --manifest docs/donor-example.json --output docs/evidence/PORT-example
The manifest specifies task, sources (path + symbols), test_inputs and notices.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import subprocess


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git(donor: Path, *args: str) -> bytes:
    env = os.environ.copy()
    # Read-only git commands must not opportunistically refresh the index.
    env["GIT_OPTIONAL_LOCKS"] = "0"
    return subprocess.run(["git", "-C", str(donor), *args], env=env,
                          check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout


def selected_file(donor: Path, name: str) -> Path:
    if not isinstance(name, str) or not name or "\\" in name or ":" in name:
        raise ValueError("Selected paths must be nonempty repository-relative POSIX paths")
    relative = PurePosixPath(name)
    if relative.is_absolute() or any(p in ("..", ".git") for p in relative.parts):
        raise ValueError(f"Unsafe selected path: {name}")
    if relative.as_posix() != name or any(ord(c) < 32 for c in name):
        raise ValueError(f"Noncanonical selected path: {name}")
    path = donor.joinpath(*relative.parts)
    # Do not follow symlinks, even if their target happens to stay inside the
    # donor today. The snapshot is about selected regular source files.
    current = donor
    for part in relative.parts:
        current /= part
        if current.is_symlink():
            raise ValueError(f"Symlink selections are not supported: {name}")
    if not path.resolve().is_relative_to(donor) or not path.is_file():
        raise ValueError(f"Selected file is missing or outside donor: {name}")
    return path


def capture(donor: Path, manifest_path: Path, output: Path) -> dict:
    donor = donor.resolve()
    output = output.resolve()
    if output.is_relative_to(donor):
        raise ValueError("Evidence output must be outside the read-only donor checkout")
    if output.exists():
        raise ValueError("Evidence output already exists; choose a new evidence directory")
    repository = Path(os.fsdecode(git(donor, "rev-parse", "--show-toplevel")).strip()).resolve()
    if repository != donor:
        raise ValueError("--donor must identify the Git worktree root")
    manifest_bytes = manifest_path.read_bytes()
    manifest = json.loads(manifest_bytes)
    if not isinstance(manifest, dict) or not isinstance(manifest.get("task"), str) or not manifest["task"].strip():
        raise ValueError("Manifest must have a nonempty task name")
    sources = manifest.get("sources")
    if not isinstance(sources, list) or not sources:
        raise ValueError("Manifest must select at least one source")
    records = []
    for role in ("sources", "test_inputs", "notices"):
        entries = manifest.get(role, [])
        if not isinstance(entries, list):
            raise ValueError(f"{role} must be a list")
        for entry in entries:
            if not isinstance(entry, dict) or "path" not in entry:
                raise ValueError(f"Each {role} entry must have a path")
            path = selected_file(donor, entry["path"])
            symbols = entry.get("symbols", [])
            if not isinstance(symbols, list) or any(not isinstance(s, str) or not s for s in symbols):
                raise ValueError("symbols must be a list of nonempty strings")
            if role == "sources" and not symbols:
                raise ValueError("Source entries must identify at least one symbol")
            records.append((role, entry, path))
    names = sorted({entry["path"] for _, entry, _ in records})
    pathspecs = [f":(literal){name}" for name in names]

    def state():
        return {
            "commit": git(donor, "rev-parse", "HEAD").decode("ascii").strip(),
            "staged.patch": git(donor, "diff", "--cached", "--binary", "--no-color", "--no-ext-diff", "--no-textconv", "HEAD", "--", *pathspecs),
            "unstaged.patch": git(donor, "diff", "--binary", "--no-color", "--no-ext-diff", "--no-textconv", "--", *pathspecs),
            "status": git(donor, "status", "--porcelain=v1", "--untracked-files=all", "--", *pathspecs),
        }

    before = state()
    contents = {name: selected_file(donor, name).read_bytes() for name in names}
    after = state()
    if before != after or any(selected_file(donor, name).read_bytes() != data for name, data in contents.items()):
        raise ValueError("Selected donor state changed during capture; retry when those files are stable")
    evidence = {
        "schema_version": 1,
        "task": manifest["task"],
        "donor_commit": before["commit"],
        "manifest_sha256": sha256(manifest_bytes),
        "selected_git_status": os.fsdecode(before["status"]),
        "files": [{"role": role, "path": entry["path"],
                   "snapshot": "files/" + entry["path"],
                   "sha256": sha256(contents[entry["path"]]),
                   "size_bytes": len(contents[entry["path"]]),
                   "symbols": entry.get("symbols", []),
                   "notes": entry.get("notes", "")}
                  for role, entry, _ in records],
        "patches": {name: {"sha256": sha256(before[name]), "size_bytes": len(before[name])}
                    for name in ("staged.patch", "unstaged.patch")},
        "limitations": ["Only explicitly selected files are frozen; this is not a full donor checkout.",
                        "Symbols and license scope are reviewer-supplied metadata, not independently verified.",
                        "Untracked selected files are in snapshots/status, not Git patches.",
                        "No tests or donor build are executed; snapshot presence is not port acceptance.",
                        "No notices were selected; license evidence remains incomplete." if not manifest.get("notices")
                        else "Selected notices are copied verbatim; applicability requires human review."],
    }
    # No donor writes have occurred. Refuse an existing destination rather
    # than overwriting a previous evidence bundle.
    output.mkdir(parents=True, exist_ok=False)
    for name, data in contents.items():
        destination = output / "files" / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    for name in ("staged.patch", "unstaged.patch"):
        (output / name).write_bytes(before[name])
    (output / "selection.json").write_bytes(manifest_bytes)
    (output / "provenance.json").write_text(json.dumps(evidence, indent=2, ensure_ascii=True) + "\n",
                                           encoding="utf-8", newline="\n")
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        evidence = capture(args.donor, args.manifest, args.output)
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Donor evidence capture failed: {error}\n")
    print(f"Captured {len(evidence['files'])} selected file records from {evidence['donor_commit']}; no donor writes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
