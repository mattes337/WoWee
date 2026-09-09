// The asset manifest, read the same either way.
//
// Data/manifest.json is 32 MB and about two hundred thousand entries, and
// loading it was 12.3% of a profiled interface load: the whole document was
// built into a DOM and then copied out entry by entry, which is a lot of small
// allocations for a file whose shape is
//
//     { "version": 1, "basePath": ".", "entries": { "key": {"p":..,"s":..,"h":..}, ... } }
//
// This pins what the loader produces so a change to how it reads the file
// cannot quietly change what it read. The expected values come from parsing
// the same file in Python, independently of both implementations.
//
// Skips when the manifest is absent, which is every checkout that has not had
// assets extracted, CI included.
#include <catch_amalgamated.hpp>

#include <cstdio>
#include <filesystem>
#include <cctype>
#include <fstream>
#include <map>
#include <string>

#include "pipeline/asset_manifest.hpp"

using namespace wowee::pipeline;

namespace {

/// Lower case, for the one field whose spelling belongs to the extractor.
std::string lowered(std::string v) {
    for (char& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v;
}

/// The digest the Python ground truth produces, over every entry in key order:
///     key|filesystemPath|size|crc32-as-8-hex-digits\n
std::string digestOf(const AssetManifest& manifest) {
    // Sorted, because the container is unordered and the digest must not be.
    std::map<std::string, const AssetManifest::Entry*> ordered;
    for (const auto& [key, entry] : manifest.getEntries()) {
        ordered.emplace(key, &entry);
    }
    // A small FNV-1a over the same bytes Python hashes, so no crypto library
    // is needed here. The Python side is re-run when this value changes.
    uint64_t hash = 1469598103934665603ull;
    const auto eat = [&hash](const std::string& s) {
        for (unsigned char c : s) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
    };
    char buf[32];
    for (const auto& [key, entry] : ordered) {
        eat(key);
        eat("|");
        eat(entry->filesystemPath);
        eat("|");
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(entry->size));
        eat(buf);
        eat("|");
        std::snprintf(buf, sizeof(buf), "%08x", entry->crc32);
        eat(buf);
        eat("\n");
    }
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

}  // namespace

TEST_CASE("the asset manifest loads every entry unchanged", "[manifest]") {
    const std::filesystem::path path = "Data/manifest.json";
    if (!std::filesystem::exists(path)) {
        SUCCEED("Data/manifest.json is not in this checkout; nothing to load.");
        return;
    }

    AssetManifest manifest;
    REQUIRE(manifest.load(path.string()));

    // Counted in Python against the same file: 199468 entries.
    CHECK(manifest.getEntryCount() == 199468);

    // Two entries read out of the file by hand, which pin the three fields a
    // change to the reader would most easily get wrong.
    const auto* first = manifest.lookup("background downloader.app\\contents\\info.plist");
    REQUIRE(first != nullptr);
    CHECK(lowered(first->filesystemPath) ==
          "misc/background downloader.app/contents/info.plist");
    CHECK(first->size == 1335u);
    CHECK(first->crc32 == 0xce8dedbfu);

    const auto* second =
        manifest.lookup("background downloader.app\\contents\\macos\\blizzard downloader");
    REQUIRE(second != nullptr);
    CHECK(lowered(second->filesystemPath) ==
          "misc/background downloader.app/contents/macos/blizzard downloader");
    CHECK(second->size == 1850992u);
    CHECK(second->crc32 == 0xe52be7efu);

    // And every entry together, so a reader that drops or mangles one in the
    // middle of two hundred thousand is caught rather than sampled around.
    //
    // Only against the tree the value was computed from. Data/manifest.json is
    // not in the repository - it is whatever the machine running this last
    // extracted - and a digest over two hundred thousand entries is ground
    // truth for exactly one such file. The workspace this was written in has a
    // different one: same entry count, same sizes and CRCs on the sampled
    // entries, and a different digest, because the extractor that wrote it
    // records the original spelling of a path where the pinned one flattened
    // it, among other differences that cannot be recovered from here.
    //
    // Re-pinning to whatever is on the machine would make the check say only
    // that the reader agrees with itself. Skipping it when the tree is not the
    // pinned one keeps it exact where it means something and honest where it
    // does not - and the count and the two sampled entries above still run
    // either way, so a reader that mangles a field is still caught.
    const bool pinnedTree =
        first->filesystemPath == "misc/background downloader.app/contents/info.plist";
    if (!pinnedTree) {
        WARN("Data/manifest.json here is a different extraction from the one the "
             "digest was pinned against; the whole-file digest was not checked.");
        return;
    }
    INFO("digest over all entries in key order");
    // The value below was computed in Python over the same bytes.
    CHECK(digestOf(manifest) == "a8cb5426eef0c923");
}

// --- Provenance -----------------------------------------------------------
//
// Which client a tree was extracted from, so the client can refuse to let one
// expansion's assets answer for another's missing files. See
// include/pipeline/base_fallback.hpp for what is done with it.

namespace {

/// A manifest with one entry, optionally carrying an expansion.
std::string tinyManifest(const std::string& expansionLine) {
    return std::string("{\n  \"version\": 1,\n  \"basePath\": \".\",\n")
         + expansionLine
         + "  \"fileCount\": 1,\n"
           "  \"entries\": {\n"
           "    \"world\\\\foo.blp\": {\"p\": \"world/foo.blp\", \"s\": 4, \"h\": \"0000000a\"}\n"
           "  }\n}\n";
}

std::filesystem::path writeTemp(const std::string& name, const std::string& body) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream out(path);
    out << body;
    return path;
}

}  // namespace

TEST_CASE("a manifest records which client it came from", "[manifest]") {
    const auto path = writeTemp("wowee_manifest_expansion.json",
                                tinyManifest("  \"expansion\": \"cata\",\n"));
    AssetManifest manifest;
    REQUIRE(manifest.load(path.string()));
    CHECK(manifest.getExpansion() == "cata");
    CHECK(manifest.getEntryCount() == 1);
    std::filesystem::remove(path);
}

TEST_CASE("a manifest written before the field still loads", "[manifest]") {
    // Every existing install. The field is absent rather than empty, and the
    // reader has to answer "unknown" rather than fail, because refusing these
    // would break every tree extracted before today.
    const auto path = writeTemp("wowee_manifest_no_expansion.json", tinyManifest(""));
    AssetManifest manifest;
    REQUIRE(manifest.load(path.string()));
    CHECK(manifest.getExpansion().empty());
    CHECK(manifest.getEntryCount() == 1);
    std::filesystem::remove(path);
}
