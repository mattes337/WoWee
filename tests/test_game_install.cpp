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

// ── Installation detection, against directory fixtures ──────────────────
//
// Two real installations drove these: a 3.3.5a client that detects from
// lichking.mpq, and a Turtle client whose launcher is the stock WoW.exe and
// whose highest patch is patch-4, so neither the TurtleWoW.exe check nor the
// lettered-patch check saw it and it read as plain Vanilla - the wrong auth
// build.

#include <filesystem>
#include <fstream>

namespace {

namespace fs = std::filesystem;

/// A throwaway directory tree shaped like a game installation.
class InstallFixture {
public:
    explicit InstallFixture(const std::string& name) {
        root_ = fs::temp_directory_path() / ("wowee_install_" + name + "_" +
                                             std::to_string(::rand()));
        fs::create_directories(root_ / "Data");
    }
    ~InstallFixture() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    InstallFixture(const InstallFixture&) = delete;
    InstallFixture& operator=(const InstallFixture&) = delete;

    /// An empty file standing in for an archive or an executable.
    void touch(const std::string& relative) const {
        const fs::path at = root_ / relative;
        fs::create_directories(at.parent_path());
        std::ofstream(at, std::ios::binary).put('\0');
    }

    void write(const std::string& relative, const std::string& text) const {
        const fs::path at = root_ / relative;
        fs::create_directories(at.parent_path());
        std::ofstream(at, std::ios::binary) << text;
    }

    [[nodiscard]] std::string root() const { return root_.string(); }
    [[nodiscard]] std::string dataDir() const { return (root_ / "Data").string(); }

    /// The archives every vanilla-era client has.
    void makeVanilla() const {
        for (const char* name : {"base.MPQ", "dbc.MPQ", "terrain.MPQ", "texture.MPQ",
                                 "patch.MPQ", "patch-2.MPQ"}) {
            touch(std::string("Data/") + name);
        }
        touch("WoW.exe");
    }

private:
    fs::path root_;
};

}  // namespace

TEST_CASE("wotlk is detected from lichking.mpq", "[game_install]") {
    InstallFixture install("wotlk");
    for (const char* name : {"common.MPQ", "common-2.MPQ", "expansion.MPQ", "lichking.MPQ"}) {
        install.touch(std::string("Data/") + name);
    }
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()) == "wotlk");
}

TEST_CASE("tbc is detected from expansion.mpq without lichking", "[game_install]") {
    InstallFixture install("tbc");
    install.touch("Data/common.MPQ");
    install.touch("Data/expansion.MPQ");
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()) == "tbc");
}

TEST_CASE("a stock vanilla client stays classic", "[game_install]") {
    InstallFixture install("classic");
    install.makeVanilla();
    // A community patch past stock Vanilla's patch-2 is not a Turtle signal:
    // calling it one would send auth build 7272 to a server expecting 5875.
    install.touch("Data/patch-3.MPQ");
    install.write("realmlist.wtf", "set realmlist logon.example-vanilla.org\n");
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()) == "classic");
}

TEST_CASE("turtle is detected from its own realm list", "[game_install]") {
    InstallFixture install("turtle_realmlist");
    install.makeVanilla();
    install.touch("Data/patch-3.mpq");
    install.touch("Data/patch-4.mpq");
    install.write("realmlist.wtf",
                  "set realmlist logon.turtle-wow.org\nset patchlist logon.turtle-wow.org\n");
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()) == "turtle");
}

TEST_CASE("turtle is detected from a saved config when the realm list is gone",
          "[game_install]") {
    InstallFixture install("turtle_config");
    install.makeVanilla();
    install.write("WTF/Config.wtf",
                  "SET hwDetect \"0\"\nSET realmList \"logon.turtle-wow.org\"\n");
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()) == "turtle");
}

TEST_CASE("turtle is still detected from its own launcher", "[game_install]") {
    InstallFixture install("turtle_exe");
    install.makeVanilla();
    install.touch("TurtleWoW.exe");
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()) == "turtle");
}

TEST_CASE("an explicit path may name either the root or the Data directory",
          "[game_install]") {
    InstallFixture install("explicit");
    for (const char* name : {"common.MPQ", "expansion.MPQ", "lichking.MPQ"}) {
        install.touch(std::string("Data/") + name);
    }

    const auto fromRoot = wowee::pipeline::detectGameInstall(install.root());
    REQUIRE(fromRoot.isValid());
    REQUIRE(fromRoot.expansion == "wotlk");
    REQUIRE(fromRoot.archives.size() == 3);

    const auto fromData = wowee::pipeline::detectGameInstall(install.dataDir());
    REQUIRE(fromData.isValid());
    REQUIRE(fromData.expansion == "wotlk");
    REQUIRE(fromData.archives == fromRoot.archives);
}

TEST_CASE("a directory holding no archives is not an installation",
          "[game_install]") {
    InstallFixture install("empty");
    REQUIRE_FALSE(wowee::pipeline::detectGameInstall(install.root()).isValid());
    REQUIRE(wowee::pipeline::detectExpansionAt(install.dataDir()).empty());
}

TEST_CASE("locale archives are found under the locale directory", "[game_install]") {
    InstallFixture install("locale");
    for (const char* name : {"common.MPQ", "expansion.MPQ", "lichking.MPQ"}) {
        install.touch(std::string("Data/") + name);
    }
    install.touch("Data/deDE/locale-deDE.MPQ");
    install.touch("Data/deDE/patch-deDE.MPQ");

    REQUIRE(wowee::pipeline::detectLocaleAt(install.dataDir()) == "deDE");

    const auto found = wowee::pipeline::detectGameInstall(install.root());
    REQUIRE(found.locale == "deDE");
    REQUIRE(found.archives.size() == 5);
    // A locale archive outranks the base archives; a patch outranks everything.
    REQUIRE(found.archives.back().find("patch-deDE") != std::string::npos);
    REQUIRE(found.archives[3].find("locale-deDE") != std::string::npos);
}
