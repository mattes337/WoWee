"""Drive the real login UI with SDL events against a loopback test emulator.

Credentials and generated traces must stay in an ignored private directory.
Click coordinates are explicit observations or source-derived hypotheses;
only observed production protocol markers establish success.
"""
import argparse
import json
import os
from pathlib import Path
import re
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


def add_creation_trace(trace, name, x, y, after_updates):
    """Click New Hero, focus blank name through normal validation, then submit."""
    if not re.fullmatch(r"[A-Za-z]{2,12}", name):
        raise ValueError("test character name must contain 2..12 ASCII letters")
    if after_updates <= 45 or after_updates + 35 >= trace["stop_after_updates"]:
        raise ValueError("creation input must follow login and precede shutdown")
    trace["events"].extend([
        {"after_updates": after_updates, "type": "mouse_move", "x": x, "y": y},
        {"after_updates": after_updates, "type": "mouse_down", "x": x, "y": y, "button": 1},
        {"after_updates": after_updates + 2, "type": "mouse_up", "x": x, "y": y, "button": 1},
        {"after_updates": after_updates + 10, "type": "key_down", "keycode": 13, "scancode": 40},
        {"after_updates": after_updates + 11, "type": "key_up", "keycode": 13, "scancode": 40},
        {"after_updates": after_updates + 20, "type": "text", "text": name},
        {"after_updates": after_updates + 34, "type": "key_down", "keycode": 13, "scancode": 40},
        {"after_updates": after_updates + 35, "type": "key_up", "keycode": 13, "scancode": 40},
    ])


def classify(returncode, log, updates, event_count=8, created_name=None):
    report = classify_smoke(returncode, log, updates)
    markers = {
        "intended_account": "Starting authentication for user: WOWEE_EVAL_A",
        "intended_auth_endpoint": "Connecting to auth server: 127.0.0.1:3724",
        "auth_success": "   AUTHENTICATION SUCCESSFUL!",
        "realm_list": "REALM LIST RECEIVED!",
        "world_auth_success": "AUTH_RESPONSE OK - world authentication successful",
        "character_list": "Ready to select character",
        "trace_completed": f"SDL input trace completed: {event_count} events, {updates} completed update/render iterations",
    }
    positions = [log.find(markers[key]) for key in
                 ("auth_success", "realm_list", "world_auth_success", "character_list")]
    report.update({key: marker in log for key, marker in markers.items()})
    report["protocol_order_verified"] = all(p >= 0 for p in positions) and positions == sorted(positions)
    report["result"] = "pass" if (report["result"] == "pass"
        and all(report[key] for key in markers) and report["protocol_order_verified"]) else "fail"
    if created_name:
        created = log.find("Character created successfully (code=47)")
        refreshed = log.find("CHARACTER LIST RECEIVED", created) if created >= 0 else -1
        listed = re.search(r"\[\d+\] " + re.escape(created_name) + r"(?:\r?\n|$)", log[refreshed:]) if refreshed >= 0 else None
        ready = log.find("Ready to select character", refreshed + listed.end()) if listed else -1
        report["creation_response_and_refreshed_list"] = (
            created > positions[-1] and refreshed > created and listed is not None
            and ready > refreshed and ready < log.find(markers["trace_completed"]))
        if not report["creation_response_and_refreshed_list"]:
            report["result"] = "fail"
    return report


def require_ignored(output):
    repo = Path(__file__).resolve().parents[1]
    result = subprocess.run(["git", "check-ignore", "--quiet", str(output / "input-trace.json")],
                            cwd=repo, capture_output=True)
    if result.returncode:
        raise ValueError("output must be inside a Git-ignored private evidence directory")


def run(args):
    binary_hash = sha256(args.binary)
    if binary_hash.lower() != args.expected_binary_sha256.lower():
        raise ValueError("executable hash does not match the separately validated build")
    require_ignored(args.output)
    secret = json.loads(args.secrets.read_text(encoding="utf-8"))["account_a_password"]
    trace = make_trace(secret, args.account_x, args.account_y, args.updates)
    if args.create_name:
        add_creation_trace(trace, args.create_name, args.newhero_x, args.newhero_y, args.creation_after_updates)
    args.output.mkdir(parents=True, exist_ok=False)
    runtime = args.output / "runtime"
    runtime.mkdir()
    for name in ("assets", "addons"):
        source = args.binary.parent / name
        if source.is_dir():
            shutil.copytree(source, runtime / name,
                            ignore=shutil.ignore_patterns("*.saved", "SavedVariables"))
    fragment_override = None
    if args.character_fragment_override:
        source = args.character_fragment_override
        target = runtime / "assets/shaders/character.frag.spv"
        if not target.is_file() or source.read_bytes()[:4] != b"\x03\x02\x23\x07":
            raise ValueError("fragment override needs an existing fixture shader and SPIR-V input")
        fragment_override = {"original_sha256": sha256(target),
                             "override_sha256": sha256(source),
                             "scope": "fresh runtime/assets/shaders/character.frag.spv only"}
        shutil.copyfile(source, target)
    vertex_override = None
    if args.character_vertex_override:
        source = args.character_vertex_override
        target = runtime / "assets/shaders/character.vert.spv"
        if not target.is_file() or source.read_bytes()[:4] != b"\x03\x02\x23\x07":
            raise ValueError("vertex override needs an existing fixture shader and SPIR-V input")
        vertex_override = {"original_sha256": sha256(target),
                           "override_sha256": sha256(source),
                           "scope": "fresh runtime/assets/shaders/character.vert.spv only"}
        shutil.copyfile(source, target)
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
    if args.gpu_validation:
        env["WOWEE_VULKAN_GPU_VALIDATION"] = "1"
    if args.preview_isolation:
        variable = {"no-backdrop": "WOWEE_TEST_PREVIEW_NO_BACKDROP",
                    "no-model-draw": "WOWEE_TEST_PREVIEW_NO_MODEL_DRAW"}[args.preview_isolation]
        env[variable] = "1"
    if args.screenshot:
        env["WOWEE_TEST_SCREENSHOT_PATH"] = str(args.output / "screenshot.png")
        if args.screenshot_after_updates is not None:
            env["WOWEE_TEST_SCREENSHOT_AFTER_UPDATES"] = str(args.screenshot_after_updates)
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
        report = classify(code, log, args.updates, len(trace["events"]), args.create_name)
        if args.screenshot:
            capture = args.output / "screenshot.png"
            captured = capture.is_file() and f"Screenshot saved: {capture}" in log
            scheduled = (args.screenshot_after_updates is None or
                f"Unattended screenshot queued after {args.screenshot_after_updates} completed update/render iterations" in log)
            report["screenshot"] = {"completion_logged": captured,
                                    "schedule_verified": scheduled,
                                    "sha256": sha256(capture) if captured else None,
                                    "pixels_decoded": False,
                                    "after_updates": args.screenshot_after_updates,
                                    "timing": "completed-update count; not a server-state condition" if args.screenshot_after_updates is not None else "startup capture, not final state"}
            if not captured or not scheduled:
                report["result"] = "fail"
    report.update(binary_sha256=binary_hash, expected_binary_sha256=args.expected_binary_sha256, input=identity,
                  updates=args.updates, timeout_seconds=args.timeout,
                  account="WOWEE_EVAL_A", auth_endpoint="127.0.0.1:3724",
                  input_geometry={"account_x": args.account_x, "account_y": args.account_y,
                                  "basis": args.geometry_basis},
                  requested_character=args.create_name,
                  diagnostic_mode="GPU-assisted validation requested; not normal-mode certification" if args.gpu_validation else ("shader override; not normal-mode certification" if fragment_override or vertex_override else "normal validation"),
                  preview_isolation=args.preview_isolation,
                  character_fragment_override=fragment_override,
                  character_vertex_override=vertex_override,
                  default_preview_certified=False if args.preview_isolation or fragment_override or vertex_override else None,
                  scope="real SDL input, authentication, realm and character list; optional real character creation; no world entry or gameplay certification")
    (args.output / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "assets", "profiles", "output", "secrets"):
        parser.add_argument("--" + name, type=lambda value: Path(value).resolve(), required=True)
    parser.add_argument("--layer-path", type=lambda value: Path(value).resolve())
    parser.add_argument("--expected-binary-sha256", required=True,
                        help="SHA-256 from the separately validated build/capture evidence")
    parser.add_argument("--account-x", type=int, required=True)
    parser.add_argument("--account-y", type=int, required=True)
    parser.add_argument("--geometry-basis", choices=("source-hypothesis", "observed-capture"), required=True)
    parser.add_argument("--updates", type=int, default=1800)
    parser.add_argument("--timeout", type=float, default=90)
    parser.add_argument("--execute", action="store_true", help="otherwise prepare private fixtures only")
    parser.add_argument("--screenshot", action="store_true", help="require startup capture acknowledgement; pixels need separate inspection")
    parser.add_argument("--gpu-validation", action="store_true", help="request GPU-assisted diagnostic validation; not normal-mode certification")
    parser.add_argument("--preview-isolation", choices=("no-backdrop", "no-model-draw"),
                        help="diagnostic preview isolation; cannot certify default rendering")
    parser.add_argument("--character-fragment-override", type=lambda value: Path(value).resolve(),
                        help="diagnostic SPIR-V copied only over the fresh fixture character fragment shader")
    parser.add_argument("--character-vertex-override", type=lambda value: Path(value).resolve(),
                        help="diagnostic SPIR-V copied only over the fresh fixture character vertex shader")
    parser.add_argument("--screenshot-after-updates", type=int,
                        help="delay capture by completed updates; does not wait for a server state")
    parser.add_argument("--create-name", help="optional dedicated test character to create through the UI")
    parser.add_argument("--newhero-x", type=int)
    parser.add_argument("--newhero-y", type=int)
    parser.add_argument("--creation-after-updates", type=int, default=300)
    args = parser.parse_args()
    if not 46 <= args.updates <= 1000000 or not 0 < args.timeout <= 3600:
        parser.error("updates must be 46..1000000 and timeout positive and at most 3600")
    if args.create_name and (args.newhero_x is None or args.newhero_y is None):
        parser.error("character creation requires explicit New Hero coordinates")
    if args.screenshot_after_updates is not None and (not args.screenshot or
            not 0 <= args.screenshot_after_updates < args.updates):
        parser.error("delayed capture requires --screenshot and an update count before shutdown")
    report = run(args)
    print(json.dumps({key: report[key] for key in ("result", "account", "auth_endpoint")}))
    return 0 if report["result"] in ("pass", "prepared-not-run") else 1


if __name__ == "__main__":
    raise SystemExit(main())
