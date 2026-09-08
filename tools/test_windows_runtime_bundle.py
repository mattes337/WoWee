"""Pure dependency-graph regressions for the native Windows runtime bundler."""
import importlib.util
import os
import subprocess
import sys
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("runtime_bundle", Path(__file__).parent / "windows/bundle_runtime_dlls.py")
bundle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle)


class RuntimeBundleTest(unittest.TestCase):
    def test_dumpbin_output_parser(self):
        self.assertEqual(bundle.parse_dependencies("""
  Image has the following dependencies:
    SDL2.dll
    KERNEL32.dll
    SDL2.dll
    ..\\escape.dll
  Summary
    1000 .data
"""), ["SDL2.dll", "KERNEL32.dll"])

    def test_transitive_resolution_and_configuration_precedence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            debug, release, output, system = [root / name for name in ("debug", "release", "output", "system")]
            for path in (debug, release, output, system):
                path.mkdir()
            executable = output / "wowee.exe"
            executable.touch()
            for path in (debug / "A.dll", release / "A.dll", output / "A.dll", debug / "B.dll", system / "KERNEL32.dll"):
                path.touch()
            graph = {executable: ["a.DLL", "api-ms-win-core-test-l1-1-0.dll"],
                     debug / "A.dll": ["B.dll"], debug / "B.dll": ["A.dll", "kernel32.DLL"]}
            self.assertEqual(bundle.resolve_runtime(executable, [debug, release], system, graph.__getitem__),
                             [debug / "A.dll", debug / "B.dll"])

    @unittest.skipUnless(os.name == "nt", "real dumpbin fixture requires Windows")
    def test_real_transitive_dll_imports(self):
        with tempfile.TemporaryDirectory(prefix="wowee dll chain ") as temporary:
            root = Path(temporary)
            (root / "leaf.cpp").write_text('extern "C" __declspec(dllexport) int leaf() { return 7; }')
            (root / "middle.cpp").write_text('extern "C" int leaf(); extern "C" __declspec(dllexport) int middle() { return leaf(); }')
            (root / "main.cpp").write_text('extern "C" int middle(); int main() { return middle(); }')
            (root / "CMakeLists.txt").write_text("""
cmake_minimum_required(VERSION 3.15)
project(dll_chain LANGUAGES CXX)
add_library(leaf SHARED leaf.cpp)
add_library(middle SHARED middle.cpp)
target_link_libraries(middle PRIVATE leaf)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE middle)
set_target_properties(leaf middle PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/deps")
set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/app")
""")
            build = root / "build"
            for args in (["cmake", "-S", str(root), "-B", str(build)],
                         ["cmake", "--build", str(build), "--config", "Debug"]):
                result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode, 0, result.stdout)
            executable = next((build / "app").rglob("app.exe"))
            leaf = next((build / "deps").rglob("leaf.dll"))
            result = subprocess.run([sys.executable, str(Path(bundle.__file__)), "--exe", str(executable),
                                     "--search-dir", str(leaf.parent)], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertEqual({path.name for path in executable.parent.glob("*.dll")}, {"leaf.dll", "middle.dll"})
            for name in ("leaf.dll", "middle.dll"):
                self.assertEqual((executable.parent / name).read_bytes(), (leaf.parent / name).read_bytes())

    def test_unresolved_dependency_fails_before_copying(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output, system = [root / name for name in ("source", "output", "system")]
            for path in (source, output, system):
                path.mkdir()
            executable = output / "wowee.exe"
            executable.touch()
            (source / "A.dll").touch()
            with self.assertRaisesRegex(RuntimeError, "missing.dll"):
                bundle.resolve_runtime(executable, [source], system,
                                       lambda path: ["A.dll"] if path == executable else ["missing.dll"])
            self.assertEqual(list(output.iterdir()), [executable])


if __name__ == "__main__":
    unittest.main()
