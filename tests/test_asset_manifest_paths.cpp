#include <catch_amalgamated.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "pipeline/asset_manifest.hpp"

using wowee::pipeline::AssetManifest;

namespace {
struct TempTree {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("wowee_manifest_path_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    ~TempTree() { std::filesystem::remove_all(root); }
};

std::string pathManifest(const std::filesystem::path& basePath) {
    return std::string("{\n  \"version\": 1,\n  \"basePath\": \"")
        + basePath.generic_string()
        + "\",\n  \"entries\": {\n"
          "    \"interface\\\\framexml\\\\fixture.lua\": "
          "{\"p\": \"interface/framexml/fixture.lua\", \"s\": 1, \"h\": \"0\"}\n"
          "  }\n}\n";
}
}

TEST_CASE("an absolute manifest base path remains absolute", "[manifest][path]") {
    TempTree tree;
    const auto manifests = tree.root / "manifests";
    const auto assets = tree.root / "asset-root";
    const auto file = assets / "interface" / "framexml" / "fixture.lua";
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::create_directories(manifests);
    { std::ofstream out(file); out << "x"; }
    const auto manifestPath = manifests / "manifest.json";
    { std::ofstream out(manifestPath); out << pathManifest(assets); }

    AssetManifest manifest;
    REQUIRE(manifest.load(manifestPath.string()));
    const auto resolved = manifest.resolveFilesystemPath(
        "interface\\framexml\\fixture.lua");
    CHECK(std::filesystem::path(resolved) == file.lexically_normal());
    CHECK(std::filesystem::exists(resolved));

#ifdef _WIN32
    // Exact adapter for the old production code: it only recognized a leading
    // slash, then joined with raw strings. Drive-letter paths entered here.
    const auto oldBase = manifests.string() + "/" + assets.string();
    const auto oldResolved = std::filesystem::path(
        oldBase + "/interface/framexml/fixture.lua");
    CHECK(oldResolved.lexically_normal() != std::filesystem::path(resolved));
    CHECK_FALSE(std::filesystem::exists(oldResolved));
#endif
}

TEST_CASE("a relative manifest base path resolves from its manifest", "[manifest][path]") {
    TempTree tree;
    const auto manifests = tree.root / "manifests";
    const auto assets = tree.root / "assets";
    const auto file = assets / "interface" / "framexml" / "fixture.lua";
    std::filesystem::create_directories(file.parent_path());
    std::filesystem::create_directories(manifests);
    { std::ofstream out(file); out << "x"; }
    const auto manifestPath = manifests / "manifest.json";
    { std::ofstream out(manifestPath); out << pathManifest("../assets"); }

    AssetManifest manifest;
    REQUIRE(manifest.load(manifestPath.string()));
    CHECK(std::filesystem::path(manifest.resolveFilesystemPath(
              "interface\\framexml\\fixture.lua")) == file.lexically_normal());
}
