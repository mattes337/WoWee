import unittest
import tempfile
import os
from pathlib import Path
from unittest.mock import patch
import subprocess
import struct

from client_smoke import classify, run


GOOD = """[INFO ] Asset manager initialized successfully
[INFO ] Vulkan validation layers enabled
[INFO ] Unattended smoke SDL_QUIT dispatched after 120 completed update/render iterations
[INFO ] Application exited successfully
"""


class ClientSmokeTest(unittest.TestCase):
    def test_success_requires_observed_shutdown_and_exit(self):
        self.assertEqual(classify(0, GOOD, 120)["result"], "pass")
        for code in (1, None, 3221225477):
            self.assertEqual(classify(code, GOOD, 120)["result"], "fail")
        self.assertEqual(classify(0, "", 120)["result"], "fail")
        self.assertEqual(classify(0, GOOD.replace("layers enabled", "layers requested"), 120)["result"], "fail")
        self.assertEqual(classify(0, GOOD, 121)["result"], "fail")
        self.assertEqual(classify(0, GOOD.replace("Application exited successfully", ""), 120)["result"], "fail")

    def test_inherited_overrides_cannot_escape_fixture_or_skip_rendering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "wowee.exe"
            binary.write_bytes(b"fixture")
            assets = root / "source"
            assets.mkdir()
            (assets / "manifest.json").write_text('{"basePath":"."}')
            profiles = root / "profiles"
            profiles.mkdir()
            def fake_process(command, **kwargs):
                env = kwargs["env"]
                self.assertNotIn("WOWEE_RESOURCE_ROOT", env)
                self.assertNotIn("WOWEE_FRAMEXML_EMIT_DIR", env)
                self.assertNotIn("WOWEE_SKIP_ALL_RENDER", env)
                self.assertEqual(env["WOWEE_TEST_MAX_UPDATES"], "120")
                self.assertEqual(env["WOWEE_VULKAN_VALIDATION"], "1")
                self.assertEqual(Path(env["WOWEE_CONFIG_ROOT"]), root / "result/config")
                self.assertEqual(kwargs["cwd"], root / "result/runtime")
                self.assertEqual(env["PATH"].split(os.pathsep)[0], str(root))
                return subprocess.CompletedProcess(command, 0, b"")
            with patch.dict("os.environ", {"WOWEE_RESOURCE_ROOT": "outside",
                             "WOWEE_FRAMEXML_EMIT_DIR": "outside",
                             "WOWEE_SKIP_ALL_RENDER": "1"}), \
                    patch("client_smoke.subprocess.run", side_effect=fake_process):
                result = run(binary, assets, profiles, root / "result", 120, 90)
            self.assertEqual(result["result"], "fail")  # Missing runtime log.

    def run_artifact_fixture(self, *, trace=False, trace_complete=False,
                             png=None, saved=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / "wowee.exe"
            binary.write_bytes(b"fixture")
            assets = root / "source"
            assets.mkdir()
            (assets / "manifest.json").write_text('{"basePath":"."}')
            profiles = root / "profiles"
            profiles.mkdir()
            trace_path = root / "trace.json"
            trace_path.write_bytes(b'{"fixture":"payload"}')
            def fake_process(command, **kwargs):
                log = GOOD
                if trace:
                    copied = Path(kwargs["env"]["WOWEE_TEST_INPUT_TRACE"])
                    self.assertEqual(copied.parent, root / "result")
                    self.assertEqual(copied.read_bytes(), trace_path.read_bytes())
                if trace_complete:
                    log += "[INFO ] SDL input trace completed: 2 events\n"
                capture = Path(kwargs["env"]["WOWEE_TEST_SCREENSHOT_PATH"])
                self.assertEqual(capture, root / "result/screenshot.png")
                if png is not None:
                    capture.write_bytes(png)
                if saved:
                    log += f"[INFO ] Screenshot saved: {capture}\n"
                logs = kwargs["cwd"] / "logs"
                logs.mkdir()
                (logs / "smoke.log").write_text(log)
                return subprocess.CompletedProcess(command, 0, b"")
            with patch("client_smoke.subprocess.run", side_effect=fake_process):
                return run(binary, assets, profiles, root / "result", 120, 90,
                           input_trace=trace_path if trace else None, screenshot=True)

    def test_trace_requires_completion_in_addition_to_clean_shutdown(self):
        # Deliberately just header metadata, not a decodable PNG fixture.
        header = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 2, 3)
        missing = self.run_artifact_fixture(trace=True, png=header, saved=True)
        self.assertEqual(missing["result"], "fail")
        self.assertFalse(missing["input_trace_completed"])
        complete = self.run_artifact_fixture(trace=True, trace_complete=True, png=header, saved=True)
        self.assertEqual(complete["result"], "pass")
        self.assertEqual(complete["screenshot"]["extent"], (2, 3))

    def test_capture_requires_file_metadata_and_matching_completion(self):
        header = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 13) + b"IHDR" + struct.pack(">II", 2, 3)
        for data, saved in ((None, True), (b"not a PNG", True),
                            (header[:20], True), (header[:-4] + b"\0" * 4, True),
                            (header, False)):
            with self.subTest(data=data, saved=saved):
                self.assertEqual(self.run_artifact_fixture(png=data, saved=saved)["result"], "fail")

    def test_validation_or_other_errors_cannot_hide_behind_zero_exit(self):
        result = classify(0, GOOD + "[ERROR] Vulkan: invalid command\n", 120)
        self.assertEqual(result["result"], "fail")
        self.assertEqual(len(result["errors"]), 1)
        self.assertEqual(classify(0, GOOD + "[FATAL] startup failed\n", 120)["result"], "fail")


if __name__ == "__main__":
    unittest.main()
