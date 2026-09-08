"""Small real CMake/glslc fixture for shader source and runtime isolation.

Run with --glslc PATH. Requires CMake and an available default C++ toolchain.
No game data, full-client libraries, display or Vulkan device are needed.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--glslc", required=True, type=Path)
    args = parser.parse_args()
    module = Path(__file__).resolve().parents[1] / "cmake/Shaders.cmake"
    with tempfile.TemporaryDirectory(prefix="wowee shader fixture ") as temporary:
        source = Path(temporary) / "source"
        shaders = source / "assets/shaders"
        shaders.mkdir(parents=True)
        fallback = shaders / "fixture.frag.spv"
        fallback.write_bytes(b"tracked fallback")
        shader = shaders / "fixture.frag.glsl"
        shader.write_text("#version 450\nlayout(location=0) out vec4 c;\n"
                          "void main(){c=vec4(1,0,0,1);}\n")
        (source / "main.cpp").write_text("int main() { return 0; }\n")
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.15)\nproject(shader_fixture LANGUAGES CXX)\n'
            'set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")\n'
            f'include("{module.as_posix()}")\n'
            'add_executable(fixture main.cpp)\n'
            'if(GLSLC)\ncompile_shaders(fixture)\nendif()\n'
            'sync_runtime_assets(fixture)\n')
        build = Path(temporary) / "build"

        def run(*command):
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(result.stdout)

        def configure(debug="OFF", compiler=None):
            run("cmake", "-S", str(source), "-B", str(build),
                f"-DGLSLC={compiler if compiler is not None else args.glslc.resolve().as_posix()}",
                f"-DWOWEE_SHADER_DEBUG_INFO={debug}")

        def rebuild():
            run("cmake", "--build", str(build), "--config", "Debug", "--target", "fixture")
            runtime = list((build / "bin").rglob("fixture.frag.spv"))
            assert len(runtime) == 1, runtime
            assert fallback.read_bytes() == b"tracked fallback", "build changed source fallback"
            return runtime[0].read_bytes()

        configure()
        first = rebuild()
        assert first != fallback.read_bytes()
        shader.write_text(shader.read_text().replace("1,0,0,1", "0,1,0,1"))
        second = rebuild()
        assert second != first, "shader-only build did not refresh runtime"
        configure("ON")
        debug = rebuild()
        assert debug != second, "debug option failed to rebuild shader"
        configure()
        assert rebuild() == second, "optimized reconfigure did not restore output"
        configure(compiler="")
        assert rebuild() == fallback.read_bytes(), "no-compiler fallback changed"
    print("PASS: source isolation, shader-only refresh, debug reconfigure, fallback")


if __name__ == "__main__":
    main()
