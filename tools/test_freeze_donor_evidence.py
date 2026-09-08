#!/usr/bin/env python3
"""Synthetic donor fixtures; never reads or changes the real Rust donor."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from freeze_donor_evidence import capture, git


class DonorEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.donor = self.root / "donor"
        self.donor.mkdir()
        git(self.donor, "init", "-q")
        (self.donor / "source.rs").write_bytes(b"fn selected() { /* committed */ }\n")
        (self.donor / "unrelated.txt").write_bytes(b"private unrelated fixture\n")
        (self.donor / "LICENSE").write_bytes(b"fixture license notice\n")
        git(self.donor, "add", "--", "source.rs", "unrelated.txt", "LICENSE")
        git(self.donor, "-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
            "-c", "commit.gpgsign=false", "commit", "-qm", "fixture baseline")
        self.selection = {
            "task": "PORT-fixture",
            "sources": [{"path": "source.rs", "symbols": ["selected"]}],
            "notices": [{"path": "LICENSE", "notes": "Fixture only"}],
        }
        self.manifest = self.root / "selection.json"

    def freeze(self, destination="evidence"):
        self.manifest.write_text(json.dumps(self.selection), encoding="utf-8")
        return capture(self.donor, self.manifest, self.root / destination)

    def test_dirty_donor_is_unchanged_and_only_selected_bytes_are_copied(self):
        (self.donor / "source.rs").write_bytes(b"fn selected() { /* staged */ }\n")
        git(self.donor, "add", "--", "source.rs")
        current = b"fn selected() { /* unstaged */ }\r\n"
        (self.donor / "source.rs").write_bytes(current)
        (self.donor / "unrelated.txt").write_bytes(b"unrelated dirty private content\n")
        (self.donor / "untracked-private.txt").write_bytes(b"unselected\n")
        before = {p.relative_to(self.donor): p.read_bytes() for p in self.donor.rglob("*") if p.is_file()}
        evidence = self.freeze()
        after = {p.relative_to(self.donor): p.read_bytes() for p in self.donor.rglob("*") if p.is_file()}
        self.assertEqual(before, after, "Capture modified donor bytes, index or metadata")
        self.assertEqual((self.root / "evidence/files/source.rs").read_bytes(), current)
        self.assertEqual(sorted(p.relative_to(self.root / "evidence/files").as_posix()
                                for p in (self.root / "evidence/files").rglob("*") if p.is_file()),
                         ["LICENSE", "source.rs"])
        self.assertEqual(evidence["files"][0]["sha256"], hashlib.sha256(current).hexdigest())
        for patch in ("staged.patch", "unstaged.patch"):
            data = (self.root / "evidence" / patch).read_bytes()
            self.assertIn(b"source.rs", data)
            self.assertNotIn(b"unrelated", data)
        self.assertNotIn("unrelated", evidence["selected_git_status"])

    def test_repeated_capture_is_reproducible(self):
        self.freeze("one")
        self.freeze("two")
        def contents(directory):
            return {p.relative_to(directory): p.read_bytes() for p in directory.rglob("*") if p.is_file()}
        self.assertEqual(contents(self.root / "one"), contents(self.root / "two"))

    def test_untracked_test_input_and_missing_notice_are_explicit(self):
        (self.donor / "fixture.bin").write_bytes(b"\0\xff\r\n")
        self.selection["test_inputs"] = [{"path": "fixture.bin"}]
        self.selection["notices"] = []
        evidence = self.freeze()
        self.assertEqual((self.root / "evidence/files/fixture.bin").read_bytes(), b"\0\xff\r\n")
        self.assertIn("?? fixture.bin", evidence["selected_git_status"])
        self.assertTrue(any("license evidence remains incomplete" in line for line in evidence["limitations"]))

    def test_refuses_output_inside_donor_and_existing_output(self):
        with self.assertRaisesRegex(ValueError, "outside"):
            self.freeze("donor/evidence")
        self.freeze()
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.freeze()

    def test_refuses_unselected_path_escape_or_directory(self):
        for path in ("../selection.json", "/source.rs", ".git/config", "C:/source.rs", "missing.rs", "."):
            with self.subTest(path=path):
                self.selection["sources"][0]["path"] = path
                with self.assertRaises(ValueError):
                    self.freeze()
                self.assertFalse((self.root / "evidence").exists())

    def test_source_symbol_metadata_is_required(self):
        self.selection["sources"][0]["symbols"] = []
        with self.assertRaisesRegex(ValueError, "symbol"):
            self.freeze()

    def test_concurrent_selected_change_is_rejected_before_output(self):
        reads = 0

        def changing_git(donor, *args):
            nonlocal reads
            if args == ("rev-parse", "HEAD"):
                reads += 1
                if reads == 2:
                    (self.donor / "source.rs").write_bytes(b"concurrent donor edit\n")
            return git(donor, *args)

        with patch("freeze_donor_evidence.git", side_effect=changing_git):
            with self.assertRaisesRegex(ValueError, "changed during capture"):
                self.freeze()
        self.assertFalse((self.root / "evidence").exists())
        self.assertEqual((self.donor / "source.rs").read_bytes(), b"concurrent donor edit\n")

    def test_symlink_source_is_rejected(self):
        link = self.donor / "linked.rs"
        try:
            link.symlink_to(self.donor / "source.rs")
        except OSError:
            self.skipTest("Creating symlinks is unavailable on this host")
        self.selection["sources"][0]["path"] = "linked.rs"
        with self.assertRaisesRegex(ValueError, "Symlink"):
            self.freeze()


if __name__ == "__main__":
    unittest.main()
