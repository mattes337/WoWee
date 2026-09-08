"""Validate active-configuration DLL installation through the real CMake helper."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RuntimeInstallTest(unittest.TestCase):
    def test_active_configuration_and_flat_output(self):
        ninja = shutil.which("ninja")
        if not ninja and os.name == "nt":
            candidates = list(Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")).glob(
                "Microsoft Visual Studio/*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"))
            ninja = str(candidates[0]) if candidates else None
        self.assertIsNotNone(ninja, "Ninja is required to verify both single- and multi-config generators")
        for generator, shared_output in (("Ninja Multi-Config", False), ("Ninja Multi-Config", True), ("Ninja", True)):
            with self.subTest(generator=generator, shared_output=shared_output), tempfile.TemporaryDirectory(prefix="wowee runtime ") as temporary:
                root = Path(temporary)
                source = root / "source"
                source.mkdir()
                paths = {config: root / "runtime" / ("shared" if shared_output else config)
                         for config in ("Debug", "Release")}
                default = root / "runtime" / "fallback"
                content = [
                    'cmake_minimum_required(VERSION 3.15)',
                    'project(runtime_install NONE)',
                    'add_executable(runtime IMPORTED)',
                    f'set_target_properties(runtime PROPERTIES IMPORTED_LOCATION "{default.as_posix()}/runtime.exe")',
                    f'include("{(ROOT / "cmake/WindowsRuntime.cmake").as_posix()}")',
                ]
                for config, path in paths.items():
                    content.append(f'set_target_properties(runtime PROPERTIES IMPORTED_LOCATION_{config.upper()} "{path.as_posix()}/runtime.exe")')
                content.append('wowee_install_runtime_dlls(runtime)')
                (source / "CMakeLists.txt").write_text("\n".join(content))
                build = root / "build"
                self.run_cmake("-S", str(source), "-B", str(build), "-G", generator,
                               f"-DCMAKE_MAKE_PROGRAM={ninja}", "-DCMAKE_BUILD_TYPE=Release")
                # Bundling happens after configure. Include an unrelated nested DLL
                # and sibling config to catch recursive copies and config leakage.
                for path in set(paths.values()) | {default}:
                    (path / "plugins").mkdir(parents=True)
                    (path / "runtime.dll").write_text(path.name)
                    (path / "notes.txt").write_text("not a runtime dependency")
                    (path / "plugins" / "nested.dll").write_text("not a top-level dependency")
                for config, path in paths.items():
                    stage = root / ("install " + config)
                    self.run_cmake("--install", str(build), "--config", config, "--prefix", str(stage))
                    files = sorted(p.relative_to(stage).as_posix() for p in stage.rglob("*") if p.is_file())
                    self.assertEqual(files, ["bin/runtime.dll"])
                    self.assertEqual((stage / "bin/runtime.dll").read_text(), path.name)

    def run_cmake(self, *args):
        result = subprocess.run(["cmake", *args], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
