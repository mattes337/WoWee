// Archive load order per build.
//
// The order is the whole of archive precedence: the client and the extractor
// both read it lowest priority first, and a patch that lands in the wrong place
// in this list is a file served from the archive it was meant to replace.

#include "catch_amalgamated.hpp"

#include "pipeline/game_install.hpp"

#include <algorithm>

using wowee::pipeline::archiveSequence;

namespace {

// Position in the load order, or -1 when absent.
int indexOf(const std::vector<std::string>& sequence, const std::string& name) {
    auto it = std::find(sequence.begin(), sequence.end(), name);
    return it == sequence.end() ? -1 : static_cast<int>(it - sequence.begin());
}

}  // namespace

TEST_CASE("classic loads the vanilla archive set", "[game_install]") {
    const auto sequence = archiveSequence("classic", "");

    REQUIRE(indexOf(sequence, "base.mpq") >= 0);
    REQUIRE(indexOf(sequence, "dbc.mpq") >= 0);
    REQUIRE(indexOf(sequence, "terrain.mpq") >= 0);
    // Vanilla has no expansion archives.
    REQUIRE(indexOf(sequence, "expansion.mpq") == -1);
    REQUIRE(indexOf(sequence, "lichking.mpq") == -1);
    // Patches come after every base archive.
    REQUIRE(indexOf(sequence, "patch.mpq") > indexOf(sequence, "wmo.mpq"));
}

TEST_CASE("turtle loads the same archives as vanilla", "[game_install]") {
    REQUIRE(archiveSequence("turtle", "enUS") == archiveSequence("classic", "enUS"));
}

TEST_CASE("tbc stops at expansion.mpq", "[game_install]") {
    const auto sequence = archiveSequence("tbc", "");

    REQUIRE(indexOf(sequence, "common.mpq") >= 0);
    REQUIRE(indexOf(sequence, "expansion.mpq") > indexOf(sequence, "common.mpq"));
    REQUIRE(indexOf(sequence, "lichking.mpq") == -1);
}

TEST_CASE("wotlk loads base then expansion then lichking", "[game_install]") {
    const auto sequence = archiveSequence("wotlk", "");

    REQUIRE(indexOf(sequence, "common.mpq") >= 0);
    REQUIRE(indexOf(sequence, "common-2.mpq") > indexOf(sequence, "common.mpq"));
    REQUIRE(indexOf(sequence, "expansion.mpq") > indexOf(sequence, "common-2.mpq"));
    REQUIRE(indexOf(sequence, "lichking.mpq") > indexOf(sequence, "expansion.mpq"));
}

TEST_CASE("locale archives are named lowercase under the locale directory",
          "[game_install]") {
    const auto sequence = archiveSequence("wotlk", "deDE");

    // The locale directory's real spelling is resolved on disk; the sequence
    // asks for it lowercased.
    REQUIRE(indexOf(sequence, "dede/locale-dede.mpq") >= 0);
    REQUIRE(indexOf(sequence, "dede/lichking-locale-dede.mpq") >= 0);
    // Locale archives outrank the base archives they localize.
    REQUIRE(indexOf(sequence, "dede/locale-dede.mpq") > indexOf(sequence, "lichking.mpq"));
}

TEST_CASE("patch tiers rise in order, locale patch above its base patch",
          "[game_install]") {
    const auto sequence = archiveSequence("wotlk", "enUS");

    const int patch = indexOf(sequence, "patch.mpq");
    const int patchLocale = indexOf(sequence, "enus/patch-enus.mpq");
    const int patch2 = indexOf(sequence, "patch-2.mpq");
    const int patch3 = indexOf(sequence, "patch-3.mpq");
    const int patchZ = indexOf(sequence, "patch-z.mpq");

    REQUIRE(patch >= 0);
    REQUIRE(patchLocale > patch);
    REQUIRE(patch2 > patchLocale);
    REQUIRE(patch3 > patch2);
    // Turtle's lettered patches sit above every numbered tier.
    REQUIRE(patchZ > patch3);
}

TEST_CASE("no locale means no locale archives", "[game_install]") {
    for (const auto& expansion : {"classic", "tbc", "wotlk", "turtle"}) {
        for (const auto& name : archiveSequence(expansion, "")) {
            REQUIRE(name.find('/') == std::string::npos);
        }
    }
}

TEST_CASE("an unknown expansion falls back to the fullest archive set",
          "[game_install]") {
    // Detection returning "" must not produce an empty load order: the widest
    // set still finds a client's base archives.
    REQUIRE_FALSE(archiveSequence("", "").empty());
}
