import unittest
import tempfile
import subprocess
import sys
from unittest.mock import patch
from pathlib import Path
from types import SimpleNamespace

from live_login_check import (PREVIEW_ISOLATION_ENV, add_creation_trace, add_preview_retry_trace, classify,
                              classify_missing_fragment_failure, diagnostic_mode,
                              make_trace, remove_character_fragment, require_private_output, run)


GOOD = """[INFO ] Asset manager initialized successfully
[INFO ] Vulkan validation layers enabled
[INFO ] Connecting to auth server: 127.0.0.1:3724
[INFO ] Starting authentication for user: WOWEE_EVAL_A
[INFO ]    AUTHENTICATION SUCCESSFUL!
[INFO ] REALM LIST RECEIVED!
[INFO ] AUTH_RESPONSE OK - world authentication successful
[INFO ] CHARACTER LIST RECEIVED
[INFO ] Ready to select character
[INFO ] Unattended smoke SDL_QUIT dispatched after 1800 completed update/render iterations
[INFO ] SDL input trace completed: 8 events, 1800 completed update/render iterations; not presented-frame assertions
[INFO ] Application exited successfully
"""


class LiveLoginTest(unittest.TestCase):
    def test_missing_fragment_rejects_single_sample_mode_at_cli(self):
        script = Path(__file__).with_name("live_login_check.py")
        result = subprocess.run([
            sys.executable, str(script),
            "--binary", "unused.exe", "--assets", "unused-assets",
            "--profiles", "unused-profiles", "--output", "unused-output",
            "--secrets", "unused-secrets.json", "--expected-binary-sha256", "0" * 64,
            "--account-x", "1", "--account-y", "1",
            "--geometry-basis", "source-hypothesis",
            "--missing-character-fragment", "--preview-single-sample",
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("missing fragment cannot be combined with the single-sample diagnostic",
                      result.stderr)

    def test_external_private_output_requires_contained_fresh_child(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / "private"
            root.mkdir()
            output = root / "run-19"
            require_private_output(output, root)
            output.mkdir()
            with self.assertRaisesRegex(ValueError, "fresh path"):
                require_private_output(output, root)
            with self.assertRaisesRegex(ValueError, "fresh child"):
                require_private_output(base / "escaped", root)
            with self.assertRaisesRegex(ValueError, "fresh child"):
                require_private_output(root, root)

    def test_external_private_output_rejects_enclosing_git_worktree(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "private"
            root.mkdir()
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            with self.assertRaisesRegex(ValueError, "Git worktree"):
                require_private_output(root / "run-19", root)

    def test_external_private_output_fails_closed_on_git_error(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "private"
            root.mkdir()
            failed = subprocess.CompletedProcess([], 128, "", "fatal: detected dubious ownership")
            with patch("live_login_check.subprocess.run", return_value=failed):
                with self.assertRaisesRegex(ValueError, "could not verify"):
                    require_private_output(root / "run-19", root)

    def test_missing_fragment_mutates_only_fresh_runtime_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "source.frag.spv"
            source.write_bytes(b"shader")
            runtime = root / "runtime"
            target = runtime / "assets/shaders/character.frag.spv"
            target.parent.mkdir(parents=True)
            target.write_bytes(source.read_bytes())
            identity = remove_character_fragment(runtime)
            self.assertFalse(target.exists())
            self.assertEqual(source.read_bytes(), b"shader")
            self.assertTrue(identity["removed"])
            self.assertEqual(identity["scope"], "fresh runtime/assets/shaders/character.frag.spv only")

    def test_missing_fragment_requires_exact_errors_and_zero_allocations(self):
        failure = GOOD.replace(
            "[INFO ] Ready to select character",
            "[ERROR] Failed to open shader file: assets/shaders/character.frag.spv "
            "(No such file or directory; working directory C:/fixture)\n"
            "[ERROR] CharacterRenderer: failed to create character fragment shader (vk=-3)\n"
            "[ERROR] CharacterPreview: failed to initialize CharacterRenderer\n"
            "[INFO ] shutdown: VMA still holds 0 allocations in 3 blocks (0 MB)\n"
            "[INFO ] Ready to select character")
        result = classify_missing_fragment_failure(0, failure, 1800)
        self.assertEqual(result["result"], "expected-failure-pass")
        self.assertEqual(result["vma_allocation_count"], 0)
        self.assertEqual(result["vma_block_count"], 3)
        self.assertEqual(result["intentional_error_counts"], [1, 1, 1])
        doubled = failure.replace(
            "[INFO ] shutdown: VMA still holds",
            "[ERROR] Failed to open shader file: assets/shaders/character.frag.spv (missing)\n"
            "[ERROR] CharacterRenderer: failed to create character fragment shader (vk=-3)\n"
            "[ERROR] CharacterPreview: failed to initialize CharacterRenderer\n"
            "[INFO ] shutdown: VMA still holds")
        self.assertEqual(classify_missing_fragment_failure(0, doubled, 1800)["result"], "fail")
        twice = classify_missing_fragment_failure(0, doubled, 1800, expected_attempts=2)
        self.assertEqual(twice["result"], "expected-failure-pass")
        self.assertEqual(twice["intentional_error_counts"], [2, 2, 2])
        with self.assertRaises(ValueError):
            classify_missing_fragment_failure(0, failure, 1800, expected_attempts=0)
        self.assertEqual(
            classify_missing_fragment_failure(0, failure.replace("0 allocations", "1 allocations"), 1800)["result"],
            "fail")
        self.assertEqual(
            classify_missing_fragment_failure(0, failure + "\n[ERROR] Device lost", 1800)["result"],
            "fail")
        without_startup = failure.replace("[INFO ] Asset manager initialized successfully\n", "")
        self.assertEqual(classify_missing_fragment_failure(0, without_startup, 1800)["result"], "fail")
        without_validation = failure.replace("[INFO ] Vulkan validation layers enabled\n", "")
        self.assertEqual(classify_missing_fragment_failure(0, without_validation, 1800)["result"], "fail")
        without_auth = "\n".join(line for line in failure.splitlines()
                                  if not any(marker in line for marker in
                                             ("Connecting to auth server", "Starting authentication",
                                              "AUTHENTICATION SUCCESSFUL", "REALM LIST RECEIVED",
                                              "AUTH_RESPONSE OK", "CHARACTER LIST RECEIVED",
                                              "Ready to select character")))
        self.assertEqual(classify_missing_fragment_failure(0, without_auth, 1800)["result"], "fail")
        quit_line = "[INFO ] Unattended smoke SDL_QUIT dispatched after 1800 completed update/render iterations\n"
        shutdown_line = "[INFO ] Application exited successfully\n"
        reversed_shutdown = failure.replace(quit_line, "").replace(
            shutdown_line, shutdown_line + quit_line)
        self.assertEqual(classify_missing_fragment_failure(0, reversed_shutdown, 1800)["result"], "fail")

    def test_preview_retry_trace_is_one_click_inside_run(self):
        trace = make_trace("fixture-only-password", 640, 306, 1800)
        add_preview_retry_trace(trace, 400, 198, 900)
        self.assertEqual(trace["events"][-3:], [
            {"after_updates": 900, "type": "mouse_move", "x": 400, "y": 198},
            {"after_updates": 900, "type": "mouse_down", "x": 400, "y": 198, "button": 1},
            {"after_updates": 902, "type": "mouse_up", "x": 400, "y": 198, "button": 1},
        ])
        with self.assertRaises(ValueError):
            add_preview_retry_trace(trace, 400, 198, 1798)

    def test_non_indexed_preview_isolation_is_default_off_and_uncertified(self):
        self.assertEqual(PREVIEW_ISOLATION_ENV["non-indexed-draw"],
                         "WOWEE_TEST_PREVIEW_NON_INDEXED_DRAW")
        normal = SimpleNamespace(gpu_validation=False, preview_isolation=None)
        self.assertEqual(diagnostic_mode(normal, None, None), "normal validation")
        isolated = SimpleNamespace(gpu_validation=False,
                                   preview_isolation="non-indexed-draw")
        self.assertEqual(diagnostic_mode(isolated, None, object()),
                         "preview isolation: non-indexed-draw; shader override; "
                         "not normal-mode certification")
        missing = SimpleNamespace(gpu_validation=False, preview_isolation=None)
        self.assertEqual(diagnostic_mode(missing, None, None, True),
                         "missing character fragment expected failure; not normal-mode certification")
        combined = SimpleNamespace(gpu_validation=False,
                                   preview_isolation="non-indexed-draw",
                                   preview_rasterizer_discard=True,
                                   preview_single_sample=True)
        self.assertEqual(diagnostic_mode(combined, None, None),
                         "preview isolation: non-indexed-draw; preview rasterizer discard; "
                         "preview single-sample target; "
                         "not normal-mode certification")

    def test_non_indexed_preview_isolation_requires_production_marker(self):
        missing = classify(0, GOOD, 1800, preview_isolation="non-indexed-draw")
        self.assertEqual(missing["result"], "fail")
        self.assertIn("missing_preview_isolation_marker", missing["failure_reasons"])
        marked = GOOD.replace(
            "[INFO ] Ready to select character",
            "[WARN ] CharacterRenderer: preview non-indexed draw diagnostic enabled\n"
            "[INFO ] Ready to select character")
        result = classify(0, marked, 1800, preview_isolation="non-indexed-draw")
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["preview_isolation_marker"])

    def test_combined_preview_diagnostics_require_both_production_markers(self):
        nonindexed = "[WARN ] CharacterRenderer: preview non-indexed draw diagnostic enabled\n"
        discard = "[WARN ] CharacterRenderer: preview rasterizer-discard diagnostic enabled\n"
        for markers in ("", nonindexed, discard):
            result = classify(0, GOOD + markers, 1800,
                              preview_isolation="non-indexed-draw",
                              preview_rasterizer_discard=True)
            self.assertEqual(result["result"], "fail")
        result = classify(0, GOOD + nonindexed + discard, 1800,
                          preview_isolation="non-indexed-draw",
                          preview_rasterizer_discard=True)
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["preview_isolation_marker"])
        self.assertTrue(result["preview_rasterizer_discard_marker"])

    def test_single_sample_requires_actual_target_marker(self):
        missing = classify(0, GOOD, 1800, preview_single_sample=True)
        self.assertEqual(missing["result"], "fail")
        self.assertIn("missing_preview_single_sample_marker", missing["failure_reasons"])
        marked = GOOD.replace(
            "[INFO ] Ready to select character",
            "[WARN ] CharacterPreview: preview single-sample diagnostic enabled\n"
            "[INFO ] Ready to select character")
        result = classify(0, marked, 1800, preview_single_sample=True)
        self.assertEqual(result["result"], "pass")
        self.assertTrue(result["preview_single_sample_marker"])

    def test_account_prefix_is_not_exact_test_account(self):
        self.assertEqual(classify(0, GOOD.replace("WOWEE_EVAL_A", "WOWEE_EVAL_A_OTHER"), 1800)["result"], "fail")

    def test_auth_success_cannot_precede_account_request(self):
        account = "[INFO ] Starting authentication for user: WOWEE_EVAL_A\n"
        self.assertEqual(classify(0, GOOD.replace(account, "") + account, 1800)["result"], "fail")

    def test_success_messages_cannot_follow_process_shutdown(self):
        shutdown = "[INFO ] Application exited successfully\n"
        self.assertEqual(classify(0, shutdown + GOOD.replace(shutdown, ""), 1800)["result"], "fail")

    def test_world_entry_is_rejected_for_login_and_expected_failure_runs(self):
        entered = GOOD.replace(
            "[INFO ] Ready to select character",
            "[INFO ] Ready to select character\n"
            "[INFO ] CMSG_PLAYER_LOGIN sent, entering world...")
        normal = classify(0, entered, 1800)
        self.assertEqual(normal["result"], "fail")
        self.assertTrue(normal["world_entry_started"])
        self.assertIn("unexpected_world_entry", normal["failure_reasons"])

        failure = entered.replace(
            "[INFO ] Ready to select character",
            "[ERROR] Failed to open shader file: assets/shaders/character.frag.spv (missing)\n"
            "[ERROR] CharacterRenderer: failed to create character fragment shader (vk=-3)\n"
            "[ERROR] CharacterPreview: failed to initialize CharacterRenderer\n"
            "[INFO ] shutdown: VMA still holds 0 allocations in 3 blocks (0 MB)\n"
            "[INFO ] Ready to select character")
        expected = classify_missing_fragment_failure(0, failure, 1800)
        self.assertEqual(expected["result"], "fail")
        self.assertTrue(expected["world_entry_started"])

    def test_timestamped_production_messages_and_missing_phase_reason(self):
        timestamped = "\n".join("[2026-09-08 19:00:00.000] " + line for line in GOOD.splitlines())
        self.assertEqual(classify(0, timestamped, 1800)["result"], "pass")
        result = classify(0, timestamped.replace("CHARACTER LIST RECEIVED", "not received"), 1800)
        self.assertIn("missing_character_list_received", result["failure_reasons"])

    def test_wrong_binary_rejected_before_reading_credentials_or_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "client.exe"
            binary.write_bytes(b"old build")
            with self.assertRaisesRegex(ValueError, "executable hash"):
                run(SimpleNamespace(binary=binary, expected_binary_sha256="0" * 64))

    def test_protocol_success_requires_every_stage_and_clean_shutdown(self):
        self.assertEqual(classify(0, GOOD, 1800)["result"], "pass")
        for line in GOOD.splitlines():
            self.assertEqual(classify(0, GOOD.replace(line, ""), 1800)["result"], "fail", line)
        for code in (None, 1, 3221225477):
            self.assertEqual(classify(code, GOOD, 1800)["result"], "fail")
        self.assertEqual(classify(0, GOOD + "[ERROR] Vulkan validation failed", 1800)["result"], "fail")

    def test_protocol_markers_out_of_order_do_not_pass(self):
        lines = GOOD.splitlines()
        lines[4], lines[5] = lines[5], lines[4]
        self.assertFalse(classify(0, "\n".join(lines), 1800)["protocol_order_verified"])

    def test_trace_preserves_separate_input_frames_and_payload(self):
        trace = make_trace("fixture-only-password", 640, 320, 1800)
        self.assertEqual(trace["version"], 1)
        self.assertEqual(trace["stop_after_updates"], 1800)
        self.assertEqual(len(trace["events"]), 8)
        self.assertEqual(trace["events"][5]["text"], "fixture-only-password")
        times = [event["after_updates"] for event in trace["events"]]
        self.assertEqual(times, sorted(times))
        self.assertEqual(times[0], times[1])  # Motion + press survive backend mouse polling.
        self.assertLess(times[-1], trace["stop_after_updates"])

    def test_password_utf8_bound_and_stop_validation(self):
        for password in ("", "x" * 32, "\0", "\u00e9" * 16):
            with self.assertRaises(ValueError):
                make_trace(password, 1, 1, 1800)
        with self.assertRaises(ValueError):
            make_trace("valid", 1, 1, 45)

    def test_creation_uses_normal_name_focus_and_validates_bounds(self):
        trace = make_trace("fixture-password", 640, 320, 1800)
        add_creation_trace(trace, "Woweetrial", 800, 450, 300)
        self.assertEqual(len(trace["events"]), 16)
        self.assertEqual(trace["events"][13]["text"], "Woweetrial")
        for name, start in (("bad1", 300), ("x" * 13, 300), ("Valid", 44), ("Valid", 1790)):
            with self.assertRaises(ValueError):
                add_creation_trace(trace, name, 1, 1, start)

    def test_creation_requires_success_then_named_character_in_refresh(self):
        log = GOOD.replace("8 events", "16 events")
        self.assertEqual(classify(0, log, 1800, 16, "Woweetrial")["result"], "fail")
        suffix = "[INFO ] CMSG_CHAR_CREATE sent for: Woweetrial\n[INFO ] Character created successfully (code=47)\n[INFO ] CHARACTER LIST RECEIVED\n[INFO ]   [1] Woweetrial\n[INFO ] Ready to select character\n"
        self.assertEqual(classify(0, log.replace("[INFO ] Unattended smoke", suffix + "[INFO ] Unattended smoke"), 1800, 16, "Woweetrial")["result"], "pass")
        self.assertEqual(classify(0, log.replace("[INFO ] Unattended smoke", suffix + "[INFO ] Unattended smoke"), 1800, 16, "Wrongname")["result"], "fail")
        for invalid in (suffix.replace("code=47", "code=46"), suffix.replace("CHARACTER LIST RECEIVED", ""), suffix.replace("Ready to select character", ""), suffix.replace("CMSG_CHAR_CREATE sent for: Woweetrial", "no request")):
            self.assertEqual(classify(0, log.replace("[INFO ] Unattended smoke", invalid + "[INFO ] Unattended smoke"), 1800, 16, "Woweetrial")["result"], "fail")


if __name__ == "__main__":
    unittest.main()
