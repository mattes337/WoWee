import unittest

from client_smoke import classify


GOOD = """[INFO ] Asset manager initialized successfully
[INFO ] Vulkan validation layers requested
[INFO ] Unattended smoke SDL_QUIT dispatched after 120 completed update/render iterations
[INFO ] Application exited successfully
"""


class ClientSmokeTest(unittest.TestCase):
    def test_success_requires_observed_shutdown_and_exit(self):
        self.assertEqual(classify(0, GOOD, 120)["result"], "pass")
        for code in (1, None, 3221225477):
            self.assertEqual(classify(code, GOOD, 120)["result"], "fail")
        self.assertEqual(classify(0, "", 120)["result"], "fail")
        self.assertEqual(classify(0, GOOD, 121)["result"], "fail")
        self.assertEqual(classify(0, GOOD.replace("Application exited successfully", ""), 120)["result"], "fail")

    def test_validation_or_other_errors_cannot_hide_behind_zero_exit(self):
        result = classify(0, GOOD + "[ERROR] Vulkan: invalid command\n", 120)
        self.assertEqual(result["result"], "fail")
        self.assertEqual(len(result["errors"]), 1)
        self.assertEqual(classify(0, GOOD + "[FATAL] startup failed\n", 120)["result"], "fail")


if __name__ == "__main__":
    unittest.main()
