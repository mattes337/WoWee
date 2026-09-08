import unittest
import tempfile
from pathlib import Path
from types import SimpleNamespace

from live_login_check import add_creation_trace, classify, make_trace, run


GOOD = """[INFO ] Asset manager initialized successfully
[INFO ] Vulkan validation layers enabled
[INFO ] Connecting to auth server: 127.0.0.1:3724
[INFO ] Starting authentication for user: WOWEE_EVAL_A
[INFO ]    AUTHENTICATION SUCCESSFUL!
[INFO ] REALM LIST RECEIVED!
[INFO ] AUTH_RESPONSE OK - world authentication successful
[INFO ] Ready to select character
[INFO ] SDL input trace completed: 8 events, 1800 completed update/render iterations
[INFO ] Unattended smoke SDL_QUIT dispatched after 1800 completed update/render iterations
[INFO ] Application exited successfully
"""


class LiveLoginTest(unittest.TestCase):
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
        for password in ("", "x" * 32, "\0", "ÃƒÆ’Ã‚Â©" * 16):
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
        suffix = "[INFO ] Character created successfully (code=47)\n[INFO ] CHARACTER LIST RECEIVED\n[INFO ]   [1] Woweetrial\n[INFO ] Ready to select character\n"
        self.assertEqual(classify(0, log.replace("[INFO ] SDL input", suffix + "[INFO ] SDL input"), 1800, 16, "Woweetrial")["result"], "pass")
        self.assertEqual(classify(0, log.replace("[INFO ] SDL input", suffix + "[INFO ] SDL input"), 1800, 16, "Wrongname")["result"], "fail")
        for invalid in (suffix.replace("code=47", "code=46"), suffix.replace("CHARACTER LIST RECEIVED", ""), suffix.replace("Ready to select character", "")):
            self.assertEqual(classify(0, log.replace("[INFO ] SDL input", invalid + "[INFO ] SDL input"), 1800, 16, "Woweetrial")["result"], "fail")


if __name__ == "__main__":
    unittest.main()
