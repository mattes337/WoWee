"""Compile the production CVar default dispatcher with a tiny Lua/window fixture.

Uses CMake's default C++ compiler (MSVC on Windows). The original 128-branch
else-if chain fails MSVC C1061 before the behavioral assertions can run.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / "src/addons/lua_system_api.cpp").read_text(encoding="utf-8")
    start = source.index("static void pushCvarDefault(")
    body = source[start:source.index("\n}\n", start) + 2]
    fixture = r'''
#include <string>
#include <iostream>
struct Window { int getWidth() { return 1280; } int getHeight() { return 720; } };
struct Services { Window* window; };
struct lua_State { Services* services; std::string value; int pushes = 0; };
static Services* getLuaServices(lua_State* L) { return L->services; }
static void lua_pushstring(lua_State* L, const char* value) { L->value = value; ++L->pushes; }
'''
    fixture += body + r'''
static int check(Services* services, const char* key, const char* expected) {
    lua_State state{services, "", 0};
    pushCvarDefault(&state, key);
    if (state.value != expected || state.pushes != 1) {
        std::cerr << key << ": " << state.value << " pushes=" << state.pushes << '\n';
        return 1;
    }
    return 0;
}
int main() {
    Window window;
    Services services{&window};
    Services noWindow{nullptr};
    int failures = 0;
    failures += check(nullptr, "sound_enablesoundwhengameisinbg", "0");
    failures += check(nullptr, "sound_enablefuturefeature", "1");
    failures += check(nullptr, "sound_mastervolume", "1");
    failures += check(nullptr, "screenwidth", "1920");
    failures += check(&noWindow, "screenheight", "1080");
    failures += check(&services, "gxresolution", "1280");
    failures += check(&services, "gxfullscreenresolution", "720");
    failures += check(nullptr, "trackerfilter", "3");
    failures += check(nullptr, "partybackgroundopacity", "0.5");
    failures += check(nullptr, "showtimestamps", "none");
    failures += check(nullptr, "chatstyle", "im");
    failures += check(nullptr, "lasttalkedtogm", "");
    failures += check(nullptr, "unknown", "0");
    return failures;
}
'''
    with tempfile.TemporaryDirectory(prefix="wowee cvar fixture ") as temporary:
        root = Path(temporary)
        (root / "probe.cpp").write_text(fixture, encoding="utf-8")
        (root / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.15)\nproject(cvar_probe LANGUAGES CXX)\n"
            "add_executable(probe probe.cpp)\nset_property(TARGET probe PROPERTY CXX_STANDARD 17)\n"
            "enable_testing()\nadd_test(NAME cvar_defaults COMMAND probe)\n")
        for command in [
            ["cmake", "-S", str(root), "-B", str(root / "build")],
            ["cmake", "--build", str(root / "build"), "--config", "Debug"],
            ["ctest", "--test-dir", str(root / "build"), "-C", "Debug", "--output-on-failure"],
        ]:
            result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(result.stdout)
    print("PASS: production dispatcher compiles and pushes one correct value for all 13 cases")


if __name__ == "__main__":
    main()
