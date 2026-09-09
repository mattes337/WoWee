// Where a resource path spelled relative to the working directory is actually
// looked for.
//
// This is the whole of the drop-in contract for files: wowee.exe placed beside
// the original game executable and launched from anywhere else has to find its
// own assets/, Data/ and addons/, and a checkout has to keep reading the copies
// the developer is editing. Getting the order wrong either way is silent - the
// client starts and reads the wrong tree, or reads nothing and comes up empty.

#include "catch_amalgamated.hpp"

#include "core/config_paths.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using wowee::core::resolveResourcePathIn;

namespace {

// A scratch pair of roots, removed with the test.
struct Roots {
    fs::path base;
    fs::path working;
    fs::path executable;

    Roots() {
        static std::atomic<int> serial{0};
        base = fs::temp_directory_path() /
               fs::path("wowee-resource-paths-" + std::to_string(serial++));
        std::error_code ec;
        fs::remove_all(base, ec);  // A previous run that was killed mid-test.
        working = base / "working";
        executable = base / "beside-the-exe";
        fs::create_directories(working);
        fs::create_directories(executable);
    }
    ~Roots() {
        std::error_code ec;
        fs::remove_all(base, ec);
    }

    void put(const fs::path& root, const std::string& relative) const {
        const fs::path at = root / relative;
        fs::create_directories(at.parent_path());
        std::ofstream(at) << "x";
    }
};

}  // namespace

TEST_CASE("the working directory wins when the file is there", "[resource_paths]") {
    Roots roots;
    roots.put(roots.working, "assets/Wowee.png");
    roots.put(roots.executable, "assets/Wowee.png");

    // Spelled back exactly as asked, so log lines and error messages read the
    // way they always have for anyone running out of a checkout.
    REQUIRE(resolveResourcePathIn("assets/Wowee.png", roots.working.string(),
                                  roots.executable.string()) == "assets/Wowee.png");
}

TEST_CASE("a file only beside the executable is found there", "[resource_paths]") {
    Roots roots;
    roots.put(roots.executable, "assets/Wowee.png");

    const std::string resolved = resolveResourcePathIn(
        "assets/Wowee.png", roots.working.string(), roots.executable.string());

    REQUIRE(resolved == (roots.executable / "assets" / "Wowee.png").string());
    REQUIRE(fs::exists(resolved));
}

TEST_CASE("a leading ./ does not survive into the anchored path", "[resource_paths]") {
    Roots roots;
    roots.put(roots.executable, "Data/expansions/wotlk/expansion.json");

    const std::string resolved = resolveResourcePathIn(
        "./Data", roots.working.string(), roots.executable.string());

    REQUIRE(resolved == (roots.executable / "Data").string());
    REQUIRE(resolved.find("/./") == std::string::npos);
}

TEST_CASE("a directory resolves the same way a file does", "[resource_paths]") {
    Roots roots;
    roots.put(roots.executable, "addons/WoweeWidgetDemo/WoweeWidgetDemo.toc");

    REQUIRE(resolveResourcePathIn("addons", roots.working.string(),
                                  roots.executable.string()) ==
            (roots.executable / "addons").string());
}

TEST_CASE("neither root has it, so the caller's own spelling comes back",
          "[resource_paths]") {
    Roots roots;

    // Not an invented path: whatever failure follows has to name the file the
    // caller asked for, not a guess at where it might have been.
    REQUIRE(resolveResourcePathIn("assets/grass_biomes.json", roots.working.string(),
                                  roots.executable.string()) ==
            "assets/grass_biomes.json");
}

TEST_CASE("an absolute path is returned untouched", "[resource_paths]") {
    Roots roots;
    const std::string absolute = (roots.executable / "assets").string();

    // WOW_DATA_PATH is usually absolute and goes through this. Anchoring it
    // would be a lie about where the operator pointed.
    REQUIRE(resolveResourcePathIn(absolute, roots.working.string(),
                                  roots.executable.string()) == absolute);
}

TEST_CASE("an unknown executable directory leaves the relative path alone",
          "[resource_paths]") {
    Roots roots;
    roots.put(roots.working, "assets/Wowee.png");

    // getExecutableDir returns "" when the platform cannot say. The working
    // directory still answers, and nothing else is invented.
    REQUIRE(resolveResourcePathIn("assets/Wowee.png", roots.working.string(), "") ==
            "assets/Wowee.png");
    REQUIRE(resolveResourcePathIn("assets/missing.png", roots.working.string(), "") ==
            "assets/missing.png");
}
