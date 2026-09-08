#!/usr/bin/env python3
"""Regressions for scoped opcode contracts and honest static API reporting."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import validate_opcode_maps as validator

TOOLS = Path(__file__).resolve().parent


class OpcodeScopeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.write("Data/opcodes/canonical.json", {"logical_opcodes": ["CMSG_REQUIRED", "CMSG_LEGACY"]})
        self.write("Data/opcodes/aliases.json", {"aliases": {"CMSG_ALIAS": "CMSG_REQUIRED"}})
        self.write("Data/expansions/wotlk/opcodes.json", {"CMSG_REQUIRED": "0x01"})
        self.write("Data/expansions/classic/opcodes.json", {"CMSG_LEGACY": "0x02"})
        self.write("required.json", {"wotlk": ["CMSG_ALIAS"]})
        (self.root / "src").mkdir()
        (self.root / "src/client.cpp").write_text("Opcode::CMSG_REQUIRED; Opcode::CMSG_LEGACY;", encoding="utf-8")

    def write(self, path, value):
        target = self.root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps(value), encoding="utf-8")

    def run_check(self, *extra):
        return subprocess.run([sys.executable, str(TOOLS / "validate_opcode_maps.py"),
                               "--root", str(self.root), *extra], capture_output=True, text=True)

    def test_scoped_contract_accepts_alias_and_preserves_legacy_warning(self):
        result = self.run_check("--expansion", "wotlk", "--strict-required",
                                "--required-opcodes", str(self.root / "required.json"))
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("missing_required=1", result.stdout)
        self.assertNotIn("[classic]", result.stdout)

    def test_missing_reviewed_requirement_fails(self):
        self.write("Data/expansions/wotlk/opcodes.json", {})
        result = self.run_check("--expansion", "wotlk", "--strict-required",
                                "--required-opcodes", str(self.root / "required.json"))
        self.assertEqual(result.returncode, 1)
        self.assertIn("CMSG_ALIAS", result.stdout)

    def test_default_strict_still_checks_all_references(self):
        self.assertEqual(self.run_check("--expansion", "wotlk", "--strict-required").returncode, 1)

    def test_bad_scope_is_rejected(self):
        self.assertEqual(self.run_check("--expansion", "typo").returncode, 2)
        self.assertEqual(self.run_check("--required-opcodes", str(self.root / "required.json")).returncode, 2)
        self.write("required.json", {"wotlk": []})
        self.assertEqual(self.run_check("--expansion", "wotlk", "--required-opcodes",
                                       str(self.root / "required.json")).returncode, 2)

    def test_absolute_root_excludes_generated_opcode_table(self):
        table = self.root / "src/game/opcode_table.cpp"
        table.parent.mkdir()
        table.write_text("Opcode::CMSG_GENERATED_ONLY;", encoding="utf-8")
        self.assertNotIn("CMSG_GENERATED_ONLY", validator.collect_code_refs(self.root.resolve()))


class ApiReportingTests(unittest.TestCase):
    def test_all_candidates_are_reported_without_claiming_functional_gaps(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "sample.lua").write_text("\n".join(f"UnresolvedFixtureName{i}()" for i in range(25)), encoding="utf-8")
            result = subprocess.run([sys.executable, str(TOOLS / "framexml_api_gap.py"), str(path)],
                                    capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("unresolved candidates  : 25", result.stdout)
        self.assertIn("UnresolvedFixtureName24", result.stdout)
        self.assertNotIn("genuinely missing", result.stdout)


if __name__ == "__main__":
    unittest.main()
