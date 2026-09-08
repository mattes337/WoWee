import json
from pathlib import Path
import tempfile
import unittest

from inspect_evaluation_data import inspect_manifest, sha256


class EvaluationDataTest(unittest.TestCase):
    def test_missing_mismatched_and_traversal_are_not_successes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            data = root / "data"
            data.mkdir()
            toc = data / "FrameXML.toc"
            toc.write_bytes(b"## Interface: 30300\n")
            secret = root / "private"
            secret.write_bytes(b"must not be included in report")
            manifest = data / "manifest.json"
            manifest.write_text(json.dumps({"basePath": ".", "entries": {
                "interface\\framexml\\framexml.toc": {"p": "FrameXML.toc", "s": 1},
                "missing": {"p": "missing", "s": 2},
                "escape": {"p": "../private"},
            }}))
            result = inspect_manifest(manifest, True)
            self.assertEqual(result["file_verification"]["missing"], 1)
            self.assertEqual(result["file_verification"]["size_mismatches"], 1)
            self.assertEqual(result["file_verification"]["outside_base"], 1)
            self.assertEqual(result["selected_files"]["interface\\framexml\\framexml.toc"]["sha256"], sha256(toc))
            self.assertNotIn("must not be included", json.dumps(result))
            self.assertEqual(inspect_manifest(data / "absent"), {"present": False})
            self.assertEqual(inspect_manifest(manifest)["file_verification"], "not-run")


if __name__ == "__main__":
    unittest.main()
