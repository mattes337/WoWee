"""Run a bounded real-client GPU startup/shutdown check with fresh local settings.

Requires an already built executable, its runtime assets/DLLs, and extracted
game data. This is an offline login-screen check, not gameplay certification.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from framexml_run_matrix import prepare_assets, sha256


def classify(returncode, log, updates):
    errors = [line for line in log.splitlines() if re.search(r"\[(?:ERROR|FATAL)\s*\]", line)]
    dispatch = f"Unattended smoke SDL_QUIT dispatched after {updates} completed update/render iterations"
    passed = (returncode == 0 and dispatch in log
              and "Application exited successfully" in log
              and "Asset manager initialized successfully" in log
              and "Vulkan validation layers requested" in log and not errors)
    return {"result": "pass" if passed else "fail", "exit_code": returncode,
            "quit_dispatched": dispatch in log,
            "shutdown_completed": "Application exited successfully" in log,
            "errors": errors}


def run(binary, assets, profiles, output, updates, timeout, layer_path=None):
    output.mkdir(parents=True, exist_ok=False)
    runtime = output / "runtime"
    runtime.mkdir()
    # Keep all relative runtime writes and SavedVariables away from user data.
    for name in ("assets", "addons"):
        source = binary.parent / name
        if source.is_dir():
            shutil.copytree(source, runtime / name,
                            ignore=shutil.ignore_patterns("*.saved", "SavedVariables"))
    fixture = runtime / "Data"
    identity = prepare_assets(assets, fixture, (assets / "manifest.json").read_text(encoding="utf-8"))
    shutil.copytree(profiles, fixture / "expansions")
    config = output / "config"
    config.mkdir()
    env = os.environ.copy()
    env.update(WOW_DATA_PATH=str(fixture), WOWEE_CONFIG_ROOT=str(config),
               WOWEE_LOG_FILE="smoke.log", WOWEE_LOG_LEVEL="info",
               WOWEE_LOAD_FRAMEXML="1", WOWEE_LUA_API_FALLBACK="0",
               WOWEE_TEST_MAX_UPDATES=str(updates), WOWEE_VULKAN_VALIDATION="1")
    if layer_path:
        env["VK_LAYER_PATH"] = str(layer_path)
    env["PATH"] = str(binary.parent) + os.pathsep + env.get("PATH", "")
    options = {}
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options["startupinfo"] = startup
    try:
        result = subprocess.run([str(binary)], cwd=runtime, env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=timeout, **options)
        returncode, stdout = result.returncode, result.stdout
    except subprocess.TimeoutExpired as error:
        returncode, stdout = None, error.stdout or b""
    (output / "stdout.log").write_bytes(stdout)
    log_path = runtime / "logs/smoke.log"
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    report = classify(returncode, log, updates)
    report.update(binary_sha256=sha256(binary), input=identity, updates=updates,
                  timeout_seconds=timeout, layer_path=str(layer_path) if layer_path else None,
                  source_revision=next((line.split("Source revision: ", 1)[1] for line in log.splitlines()
                                        if "Source revision: " in line), "unavailable"),
                  log_sha256=sha256(log_path) if log_path.is_file() else None,
                  scope="offline startup and normal shutdown; no visual/presented-frame/gameplay certification")
    (output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--profiles", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="new evidence directory")
    parser.add_argument("--updates", type=int, default=120)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--layer-path", type=Path)
    args = parser.parse_args()
    if not 1 <= args.updates <= 1000000 or not 0 < args.timeout <= 3600:
        parser.error("updates must be 1..1000000 and timeout must be positive and at most 3600 seconds")
    report = run(args.binary.resolve(), args.assets.resolve(), args.profiles.resolve(),
                 args.output.resolve(), args.updates, args.timeout,
                 args.layer_path.resolve() if args.layer_path else None)
    print(json.dumps({key: report[key] for key in ("result", "exit_code", "quit_dispatched", "shutdown_completed")}))
    return 0 if report["result"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
