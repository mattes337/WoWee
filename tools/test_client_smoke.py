import unittest
import tempfile
import os
from pathlib import Path
from unittest.mock import patch
import subprocess

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

    def test_validation_or_other_errors_cannot_hide_behind_zero_exit(self):
        result = classify(0, GOOD + "[ERROR] Vulkan: invalid command\n", 120)
        self.assertEqual(result["result"], "fail")
        self.assertEqual(len(result["errors"]), 1)
        self.assertEqual(classify(0, GOOD + "[FATAL] startup failed\n", 120)["result"], "fail")


if __name__ == "__main__":
    unittest.main()
