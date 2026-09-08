#!/usr/bin/env python3
"""Read selected MPQ entries and compare hashes; never extract or modify assets."""
import argparse
import hashlib
import importlib.metadata
import json
from pathlib import Path
import mpyq


def digest(data):
    return hashlib.sha256(data).hexdigest()


def compare(source, extracted, entries, hash_archives=False, manifest_path=None):
    archives = sorted(p for p in source.rglob("*") if p.is_file() and p.suffix.lower() == ".mpq")
    result = {"format": 1, "mpyq_version": importlib.metadata.version("mpyq"),
              "source_root": str(source.resolve()), "extracted_root": str(extracted.resolve()),
              "precedence": "not resolved; each archive independently inspected",
              "archive_identity": "optional matching-archive SHA256; no authenticated release hashes",
              "files": [], "archives": []}
    manifest = None
    if manifest_path:
        raw_manifest = manifest_path.read_bytes()
        manifest = json.loads(raw_manifest)["entries"]
        result["manifest_sha256"] = digest(raw_manifest)
    for logical, relative in entries:
        path = extracted / relative
        data = path.read_bytes()
        result["files"].append({"entry": logical, "extracted_relative": relative,
                                "size": len(data), "sha256": digest(data), "candidates": []})
    for entry in result["files"]:
        loose = source / entry["entry"].replace("\\", "/")
        entry["source_loose_file"] = {"exists": loose.is_file()}
        if loose.is_file():
            data = loose.read_bytes()
            entry["source_loose_file"].update(size=len(data), sha256=digest(data),
                matches_extracted=digest(data) == entry["sha256"])
        if manifest is not None:
            record = manifest.get(entry["entry"].lower())
            entry["manifest_entry"] = record
            if record:
                resolved = extracted / record["p"]
                if not resolved.resolve().is_relative_to(extracted.resolve()):
                    raise ValueError("manifest path escapes extracted root")
                entry["manifest_resolved_sha256"] = digest(resolved.read_bytes())
                entry["manifest_matches_selected"] = entry["manifest_resolved_sha256"] == entry["sha256"]
    for path in archives:
        stat = path.stat()
        archive = {"path": str(path.relative_to(source)), "size": stat.st_size,
                   "mtime_ns": stat.st_mtime_ns}
        result["archives"].append(archive)
        mpq = None
        try:
            mpq = mpyq.MPQArchive(str(path), listfile=False)
            for entry in result["files"]:
                hashed = mpq.get_hash_table_entry(entry["entry"])
                if hashed is None:
                    continue
                block = mpq.block_table[hashed.block_table_index]
                candidate = {"archive": archive["path"], "flags": hex(block.flags),
                             "declared_size": block.size, "locale": hashed.locale,
                             "platform": hashed.platform}
                entry["candidates"].append(candidate)
                try:
                    if block.flags & 0x00100000:
                        raise ValueError("patch-delta entry: independent final content unresolved")
                    data = mpq.read_file(entry["entry"])
                    if data is None:
                        raise ValueError("entry has no readable data")
                    if len(data) != block.size:
                        raise ValueError("decompressed length differs from block table")
                    candidate.update(size=len(data), sha256=digest(data),
                                     matches_extracted=digest(data) == entry["sha256"])
                except Exception as exc:
                    candidate["error"] = str(exc)
        except Exception as exc:
            archive["error"] = str(exc)
        finally:
            if mpq is not None:
                mpq.file.close()
    matched_archives = {c["archive"] for item in result["files"]
                        for c in item["candidates"] if c.get("matches_extracted")}
    for archive in result["archives"]:
        if hash_archives and archive["path"] in matched_archives:
            with (source / archive["path"]).open("rb") as stream:
                archive["sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--extracted", type=Path, required=True)
    parser.add_argument("--entry", action="append", required=True,
                        help="MPQ path=extracted relative path")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--hash-matching-archives", action="store_true")
    parser.add_argument("--manifest", type=Path)
    args = parser.parse_args()
    entries = [item.split("=", 1) for item in args.entry]
    if any(len(item) != 2 for item in entries):
        parser.error("each entry requires MPQ path=extracted relative path")
    destination = args.output.resolve()
    for input_root in (args.source.resolve(), args.extracted.resolve()):
        if destination.is_relative_to(input_root):
            parser.error("report output must be outside original and extracted asset trees")
    for _, relative in entries:
        if not (args.extracted / relative).resolve().is_relative_to(args.extracted.resolve()):
            parser.error("extracted entry must remain inside extracted root")
    result = compare(args.source, args.extracted, entries, args.hash_matching_archives, args.manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"Inspected {len(result['archives'])} archives and {len(result['files'])} selected entries.")
    for item in result["files"]:
        print(item["entry"], "matches", sum(c.get("matches_extracted", False) for c in item["candidates"]),
              "candidates", len(item["candidates"]))


if __name__ == "__main__":
    main()
