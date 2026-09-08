"""Bundle a Windows executable's imported DLL closure using dumpbin.

Example:
  python tools/windows/bundle_runtime_dlls.py --exe build/bin/Debug/wowee.exe \
    --search-dir dependency-prefix/debug/bin

Search directories are explicit and ordered; use the directory matching the
build configuration. System DLLs and API sets are left to Windows. Dynamic
LoadLibrary plugins are not discoverable from PE imports (Vulkan is staged by
CMake separately). This does not start the executable.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess


def parse_dependencies(output):
    return list(dict.fromkeys(match.group(1) for match in re.finditer(
        r"^\s+([A-Za-z0-9_.-]+\.dll)\s*$", output, re.MULTILINE | re.IGNORECASE)))


def find_dumpbin():
    found = shutil.which("dumpbin")
    if found:
        return Path(found)
    program_files = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
    candidates = sorted(program_files.glob(
        "Microsoft Visual Studio/*/*/VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe"), reverse=True)
    if not candidates:
        raise RuntimeError("dumpbin was not found; pass --dumpbin or install Visual Studio C++ tools")
    return candidates[0]


def directory_dlls(directory):
    if not directory.is_dir():
        raise RuntimeError(f"DLL search directory does not exist: {directory}")
    return {entry.name.casefold(): entry for entry in directory.iterdir()
            if entry.is_file() and entry.suffix.casefold() == ".dll"}


def resolve_runtime(executable, search_dirs, system_dir, scan):
    """Return source DLLs without writing anything; unresolved imports are fatal."""
    destination = executable.parent
    candidates = {}
    for directory in [*search_dirs, destination]:
        for name, path in directory_dlls(directory).items():
            candidates.setdefault(name, path)
    system_names = directory_dlls(system_dir)
    selected = {}
    pending = [executable]
    missing = set()
    while pending:
        subject = pending.pop()
        for name in scan(subject):
            key = name.casefold()
            if key.startswith(("api-ms-win-", "ext-ms-win-")) or key in selected:
                continue
            if key in candidates:
                selected[key] = candidates[key]
                pending.append(candidates[key])
            elif key not in system_names:
                missing.add(f"{name} (imported by {subject.name})")
    if missing:
        raise RuntimeError("Unresolved runtime dependencies:\n  " + "\n  ".join(sorted(missing)))
    return sorted(selected.values(), key=lambda path: path.name.casefold())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--search-dir", action="append", required=True, type=Path)
    parser.add_argument("--dumpbin", type=Path)
    args = parser.parse_args()
    executable = args.exe.resolve(strict=True)
    dumpbin = args.dumpbin or find_dumpbin()
    system_dir = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32"

    def scan(path):
        result = subprocess.run([str(dumpbin), "/nologo", "/dependents", str(path)],
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f"dumpbin failed for {path}:\n{result.stdout}")
        return parse_dependencies(result.stdout)

    sources = resolve_runtime(executable, [p.resolve() for p in args.search_dir], system_dir, scan)
    for source in sources:
        destination = executable.parent / source.name
        if source.resolve() != destination.resolve():
            shutil.copy2(source, destination)
        print(f"Bundled {source.name}")
    print(f"Resolved {len(sources)} non-system DLLs beside {executable.name}")


if __name__ == "__main__":
    main()
