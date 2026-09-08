#!/usr/bin/env python3
"""Opcode generation must be byte-stable on Windows and incremental builds."""
import os
from pathlib import Path
import tempfile
import unittest

from gen_opcode_registry import write_file


class GeneratedFileTests(unittest.TestCase):
    def test_new_file_uses_utf8_and_lf(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "nested/opcodes.inc"
            write_file(path, "// generated\nSMSG_TEST,\n")
            self.assertEqual(path.read_bytes(), b"// generated\nSMSG_TEST,\n")

    def test_identical_file_preserves_timestamp(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "opcodes.inc"
            path.write_bytes(b"SMSG_TEST,\n")
            # A fixed old timestamp makes this assertion independent of the
            # filesystem's resolution and avoids sleeps between writes.
            os.utime(path, (1000000000, 1000000000))
            before = path.stat().st_mtime_ns
            write_file(path, "SMSG_TEST,\n")
            self.assertEqual(path.stat().st_mtime_ns, before)

    def test_changed_content_replaces_existing_and_normalizes_crlf(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "opcodes.inc"
            path.write_bytes(b"SMSG_OLD,\r\n")
            write_file(path, "SMSG_TEST,\n")
            self.assertEqual(path.read_bytes(), b"SMSG_TEST,\n")

    def test_equivalent_crlf_is_not_mistaken_for_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "opcodes.inc"
            path.write_bytes(b"SMSG_TEST,\r\n")
            write_file(path, "SMSG_TEST,\n")
            self.assertEqual(path.read_bytes(), b"SMSG_TEST,\n")


if __name__ == "__main__":
    unittest.main()
