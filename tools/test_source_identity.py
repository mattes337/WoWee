"""Exercise the real CMake version generator against isolated Git histories."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class SourceIdentityTest(unittest.TestCase):
    def test_revision_dirty_and_untagged_archive(self):
        with tempfile.TemporaryDirectory(prefix="wowee-version-") as directory:
            root = pathlib.Path(directory)
            source = root / "source"
            source.mkdir()
            header = root / "version.hpp"

            def run(*args):
                return subprocess.check_output(args, cwd=source, text=True).strip()

            def generate():
                run("cmake", f"-DSRC_DIR={source}",
                    f"-DIN_FILE={ROOT / 'include/core/version.hpp.in'}",
                    f"-DOUT_FILE={header}", "-P", str(ROOT / "cmake/GitVersion.cmake"))
                return header.read_text()

            self.assertIn('kSourceRevision = "unknown"', generate())
            run("git", "init", "-q")
            run("git", "config", "user.name", "Version test")
            run("git", "config", "user.email", "version-test@example.invalid")
            tracked = source / "tracked.txt"
            tracked.write_text("first\n")
            run("git", "add", "tracked.txt")
            run("git", "-c", "commit.gpgsign=false", "commit", "-qm", "first")
            first = run("git", "rev-parse", "HEAD")
            self.assertIn(f'kSourceRevision = "{first}"', generate())
            run("git", "tag", "v1.0")
            tracked.write_text("second\n")
            self.assertIn(f'kSourceRevision = "{first}-dirty"', generate())
            run("git", "add", "tracked.txt")
            self.assertIn(f'kSourceRevision = "{first}-dirty"', generate())
            run("git", "-c", "commit.gpgsign=false", "commit", "-qm", "second")
            second = run("git", "rev-parse", "HEAD")
            generated = generate()
            self.assertIn('kVersion = "v1.0"', generated)
            self.assertIn(f'kSourceRevision = "{second}"', generated)
            (source / "local-assets.txt").write_text("untracked runtime data\n")
            timestamp = header.stat().st_mtime_ns
            self.assertEqual(generated, generate())
            self.assertEqual(timestamp, header.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
