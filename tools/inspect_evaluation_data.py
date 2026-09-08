"""Record non-secret local asset identities for ENV-03; does not certify gameplay.

Only reads explicitly selected files. It neither changes data/configuration nor
reads account settings. Use --verify-files for manifest entry existence/size checks.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def inspect_manifest(path, verify_files=False):
    if not path.is_file():
        return {"present": False}
    document = json.loads(path.read_text(encoding="utf-8"))
    entries = document.get("entries", {})
    base = (path.parent / document.get("basePath", "assets")).resolve()
    result = {
        "present": True, "sha256": sha256(path),
        "version": document.get("version"), "expansion": document.get("expansion"),
        "base_path": document.get("basePath"), "entry_count": len(entries),
        "declared_file_count": document.get("fileCount"),
        "file_verification": "not-run", "selected_files": {},
    }
    selected = {"interface\\framexml\\framexml.toc", "fonts\\frizqt__.ttf"}
    missing = mismatched = unsafe = 0
    for key, entry in entries.items():
        if not verify_files and key.lower() not in selected:
            continue
        candidate = (base / entry.get("p", "")).resolve()
        # A corrupt manifest must not make this evidence tool read outside its root.
        if not candidate.is_relative_to(base):
            unsafe += 1
            continue
        present = candidate.is_file()
        if not present:
            missing += 1
        elif entry.get("s") is not None and candidate.stat().st_size != entry["s"]:
            mismatched += 1
        if key.lower() in selected:
            result["selected_files"][key] = {
                "present": present,
                "sha256": sha256(candidate) if present else None,
            }
    if verify_files:
        result["file_verification"] = {
            "missing": missing, "size_mismatches": mismatched,
            "outside_base": unsafe,
            "content_hashes": "selected files only; manifest CRCs not certified",
        }
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data_root", type=Path)
    parser.add_argument("--verify-files", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.data_root.resolve()
    result = {
        "schema_version": 1,
        "scope": "local file evidence; client build/locale origin and live login unverified",
        "manifests": {}, "profiles": {},
    }
    manifests = [root / "manifest.json", root / "extracted/manifest.json"]
    for profile in sorted((root / "expansions").glob("*/expansion.json")):
        document = json.loads(profile.read_text(encoding="utf-8"))
        result["profiles"][profile.parent.name] = {
            "sha256": sha256(profile),
            "id": document.get("id"), "build": document.get("build"),
            "version": document.get("version"),
        }
        manifests.append(profile.parent / "manifest.json")
    for manifest in manifests:
        result["manifests"][manifest.relative_to(root).as_posix()] = inspect_manifest(
            manifest, args.verify_files)
    encoded = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8", newline="\n")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
