"""Drive the real login UI with SDL events against a loopback test emulator.

Credentials and generated traces must stay in an ignored private directory.
Click coordinates are explicit observations or source-derived hypotheses;
only observed production protocol markers establish success.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

from client_smoke import classify as classify_smoke
from framexml_run_matrix import prepare_assets, sha256


def make_trace(password, x, y, updates):
    if not password or len(password.encode("utf-8")) > 31 or "\0" in password:
        raise ValueError("test password must fit one SDL text event (1..31 UTF-8 bytes)")
    if updates <= 45:
        raise ValueError("stop must follow all login input events")
    events = [
        {"after_updates": 20, "type": "mouse_move", "x": x, "y": y},
        {"after_updates": 20, "type": "mouse_down", "x": x, "y": y, "button": 1},
        {"after_updates": 22, "type": "mouse_up", "x": x, "y": y, "button": 1},
        {"after_updates": 30, "type": "key_down", "keycode": 13, "scancode": 40},
        {"after_updates": 31, "type": "key_up", "keycode": 13, "scancode": 40},
        {"after_updates": 40, "type": "text", "text": password},
        {"after_updates": 44, "type": "key_down", "keycode": 13, "scancode": 40},
        {"after_updates": 45, "type": "key_up", "keycode": 13, "scancode": 40},
    ]
    return {"version": 1, "stop_after_updates": updates, "events": events}


def classify(returncode, log, updates):
    report = classify_smoke(returncode, log, updates)
    markers = {
        "auth_success": "   AUTHENTICATION SUCCESSFUL!",
        "realm_list": "REALM LIST RECEIVED!",
        "world_auth_success": "AUTH_RESPONSE OK - world authentication successful",
        "character_list": "Ready to select character",
        "trace_completed": f"SDL input trace completed: 8 events, {updates} completed update/render iterations",
    }
    positions = [log.find(markers[key]) for key in
                 ("auth_success", "realm_list", "world_auth_success", "character_list")]
    report.update({key: marker in log for key, marker in markers.items()})
    report["protocol_order_verified"] = all(p >= 0 for p in positions) and positions == sorted(positions)
    report["result"] = "pass" if (report["result"] == "pass"
        and all(report[key] for key in markers) and report["protocol_order_verified"]) else "fail"
    return report


def require_ignored(output):
    repo = Path(__file__).resolve().parents[1]
    result = subprocess.run(["git", "check-ignore", "--quiet", str(output / "input-trace.json")],
                            cwd=repo, capture_output=True)
    if result.returncode:
        raise ValueError("output must be inside a Git-ignored private evidence directory")


def run(args):
    require_ignored(args.output)
    secret = json.loads(args.secrets.read_text(encoding="utf-8"))["account_a_password"]
    trace = make_trace(secret, args.account_x, args.account_y, args.updates)
    args.output.mkdir(parents=True, exist_ok=False)
    runtime = args.output / "runtime"
    runtime.mkdir()
    for name in ("assets", "addons"):
        source = args.binary.parent / name
        if source.is_dir():
            shutil.copytree(source, runtime / name,
                            ignore=shutil.ignore_patterns("*.saved", "SavedVariables"))
    fixture = runtime / "Data"
    identity = prepare_assets(args.assets, fixture, (args.assets / "manifest.json").read_text(encoding="utf-8"))
    shutil.copytree(args.profiles, fixture / "expansions")
    config = args.output / "config"
    config.mkdir()
    (config / "login.cfg").write_text(
        "version=3\nactive=127.0.0.1:3724\n\n[server 127.0.0.1:3724]\n"
        "username=WOWEE_EVAL_A\nexpansion=wotlk\n", encoding="utf-8")
    trace_path = args.output / "input-trace.json"
    trace_path.write_text(json.dumps(trace), encoding="utf-8")
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("WOWEE_")}
    env.update(WOW_DATA_PATH=str(fixture), WOWEE_CONFIG_ROOT=str(config),
               WOWEE_LOG_FILE="live-login.log", WOWEE_LOG_LEVEL="info",
               WOWEE_LOAD_FRAMEXML="1", WOWEE_LUA_API_FALLBACK="0",
               WOWEE_TEST_INPUT_TRACE=str(trace_path), WOWEE_TEST_MAX_UPDATES=str(args.updates),
               WOWEE_VULKAN_VALIDATION="1")
    if args.layer_path:
        env["VK_LAYER_PATH"] = str(args.layer_path)
    env["PATH"] = str(args.binary.parent) + os.pathsep + env.get("PATH", "")
    report = {"result": "prepared-not-run"}
    if args.execute:
        options = {}
        if os.name == "nt":
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            options["startupinfo"] = startup
        try:
            process = subprocess.run([str(args.binary)], cwd=runtime, env=env,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=args.timeout, **options)
            code, stdout = process.returncode, process.stdout
        except subprocess.TimeoutExpired as error:
            code, stdout = None, error.stdout or b""
        def redact(text):
            return text.replace(secret, "[REDACTED]").replace(secret.upper(), "[REDACTED]")
        (args.output / "stdout.log").write_text(redact(stdout.decode("utf-8", errors="replace")), encoding="utf-8")
        log_path = runtime / "logs/live-login.log"
        log = redact(log_path.read_text(encoding="utf-8", errors="replace")) if log_path.is_file() else ""
        if log_path.is_file():
            log_path.write_text(log, encoding="utf-8")
        report = classify(code, log, args.updates)
    report.update(binary_sha256=sha256(args.binary), input=identity,
                  updates=args.updates, timeout_seconds=args.timeout,
                  account="WOWEE_EVAL_A", auth_endpoint="127.0.0.1:3724",
                  input_geometry={"account_x": args.account_x, "account_y": args.account_y,
                                  "basis": args.geometry_basis},
                  scope="real SDL input, authentication, realm and character list; no character creation or gameplay certification")
    (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "assets", "profiles", "output", "secrets"):
        parser.add_argument("--" + name, type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--layer-path", type=lambda value: Path(value).resolve())
    parser.add_argument("--account-x", type=int, required=True)
    parser.add_argument("--account-y", type=int, required=True)
    parser.add_argument("--geometry-basis", choices=("source-hypothesis", "observed-capture"), required=True)
    parser.add_argument("--updates", type=int, default=1800)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--execute", action="store_true", help="otherwise prepare private fixtures only")
    args = parser.parse_args()
    if not 46 <= args.updates <= 1000000 or not 0 < args.timeout <= 3600:
        parser.error("updates must be 46..1000000 and timeout positive and at most 3600")
    report = run(args)
    print(json.dumps({key: report[key] for key in ("result", "account", "auth_endpoint")}))
    return 0 if report["result"] in ("pass", "prepared-not-run") else 1


if __name__ == "__main__":
    raise SystemExit(main())
