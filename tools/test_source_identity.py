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
            source_file = root / "version.cpp"

            def run(*args):
                return subprocess.check_output(args, cwd=source, text=True).strip()

            def generate():
                run("cmake", f"-DSRC_DIR={source}",
                    f"-DIN_FILE={ROOT / 'include/core/version.cpp.in'}",
                    f"-DOUT_FILE={source_file}", "-P", str(ROOT / "cmake/GitVersion.cmake"))
                return source_file.read_text()

            self.assertIn('sourceRevision() noexcept { return "unknown"; }', generate())
            run("git", "init", "-q")
            run("git", "config", "user.name", "Version test")
            run("git", "config", "user.email", "version-test@example.invalid")
            tracked = source / "tracked.txt"
            tracked.write_text("first\n")
            run("git", "add", "tracked.txt")
            run("git", "-c", "commit.gpgsign=false", "commit", "-qm", "first")
            first = run("git", "rev-parse", "HEAD")
            self.assertIn(f'sourceRevision() noexcept {{ return "{first}"; }}', generate())
            run("git", "tag", "v1.0")
            tracked.write_text("second\n")
            self.assertIn(f'sourceRevision() noexcept {{ return "{first}-dirty"; }}', generate())
            run("git", "add", "tracked.txt")
            self.assertIn(f'sourceRevision() noexcept {{ return "{first}-dirty"; }}', generate())
            run("git", "-c", "commit.gpgsign=false", "commit", "-qm", "second")
            second = run("git", "rev-parse", "HEAD")
            generated = generate()
            self.assertIn('version() noexcept { return "v1.0"; }', generated)
            self.assertIn(f'sourceRevision() noexcept {{ return "{second}"; }}', generated)
            (source / "local-assets.txt").write_text("untracked runtime data\n")
            timestamp = source_file.stat().st_mtime_ns
            self.assertEqual(generated, generate())
            self.assertEqual(timestamp, source_file.stat().st_mtime_ns)


if __name__ == "__main__":
    unittest.main()
