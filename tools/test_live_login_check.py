import unittest

from live_login_check import classify, make_trace


GOOD = """[INFO ] Asset manager initialized successfully
[INFO ] Vulkan validation layers enabled
[INFO ]    AUTHENTICATION SUCCESSFUL!
[INFO ] REALM LIST RECEIVED!
[INFO ] AUTH_RESPONSE OK - world authentication successful
[INFO ] Ready to select character
[INFO ] SDL input trace completed: 8 events, 1800 completed update/render iterations
[INFO ] Unattended smoke SDL_QUIT dispatched after 1800 completed update/render iterations
[INFO ] Application exited successfully
"""


class LiveLoginTest(unittest.TestCase):
    def test_protocol_success_requires_every_stage_and_clean_shutdown(self):
        self.assertEqual(classify(0, GOOD, 1800)["result"], "pass")
        for line in GOOD.splitlines():
            self.assertEqual(classify(0, GOOD.replace(line, ""), 1800)["result"], "fail", line)
        for code in (None, 1, 3221225477):
            self.assertEqual(classify(code, GOOD, 1800)["result"], "fail")
        self.assertEqual(classify(0, GOOD + "[ERROR] Vulkan validation failed", 1800)["result"], "fail")

    def test_protocol_markers_out_of_order_do_not_pass(self):
        lines = GOOD.splitlines()
        lines[2], lines[3] = lines[3], lines[2]
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
        for password in ("", "x" * 32, "\0", "Ã©" * 16):
            with self.assertRaises(ValueError):
                make_trace(password, 1, 1, 1800)
        with self.assertRaises(ValueError):
            make_trace("valid", 1, 1, 45)


if __name__ == "__main__":
    unittest.main()
