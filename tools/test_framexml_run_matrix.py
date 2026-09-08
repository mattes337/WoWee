#!/usr/bin/env python3
"""Guard the CLI matrix against crediting crashes and unrelated failures."""
import unittest
import json
from pathlib import Path
import tempfile
from framexml_run_matrix import classify, prepare_assets


class ClassificationTests(unittest.TestCase):
    def test_nonzero_without_expected_error_is_not_a_pass(self):
        self.assertEqual(classify(1, "unrelated startup error", "requested failure", True, True), "wrong_failure")

    def test_crashes_and_timeout_are_not_expected_error_handling(self):
        for code, expected in [(None, "timeout"), (-11, "crash"), (0xC0000005, "crash")]:
            self.assertEqual(classify(code, "marker", "marker", True, True), expected)
        self.assertEqual(classify(3, "marker\nAssertion failed: native invariant", "marker", True, True), "crash")

    def test_failed_baseline_makes_later_negative_exit_inconclusive(self):
        self.assertEqual(classify(1, "marker", "marker", False, True), "inconclusive_failing_baseline")
        self.assertEqual(classify(2, "marker", "marker", False, False), "pass")

    def test_success_and_false_success_are_distinct(self):
        self.assertEqual(classify(0, "marker", "marker", True, True), "false_success")
        self.assertEqual(classify(0, "marker", "marker", False, False, True), "pass")
        self.assertEqual(classify(1, "marker", "marker", False, False, True), "failed_baseline")
        self.assertEqual(classify(1, "marker", "marker", True, True), "pass")

    def test_fixture_isolates_saved_variables_and_routes_original_assets(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "original"
            addon = source / "interface" / "AddOns" / "Example"
            addon.mkdir(parents=True)
            (addon / "Example.lua").write_text("assert(true)", encoding="utf-8")
            (addon / "Example.lua.saved").write_text("old player state", encoding="utf-8")
            fixture = Path(directory) / "case" / "assets"
            prepare_assets(source, fixture, json.dumps({"basePath": ".", "entries": {}}))
            copied = fixture / "interface" / "AddOns" / "Example"
            self.assertEqual((copied / "Example.lua").read_text(), "assert(true)")
            self.assertFalse((copied / "Example.lua.saved").exists())
            (copied / "Example.lua.saved").write_text("fixture state", encoding="utf-8")
            self.assertEqual((addon / "Example.lua.saved").read_text(), "old player state")
            route = json.loads((fixture / "manifest.json").read_text())["basePath"]
            self.assertEqual((fixture / route).resolve(), source.resolve())


if __name__ == "__main__":
    unittest.main()
