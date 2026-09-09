// Addon manifests.
//
// A .toc says what an addon is called, what it loads, what it cannot run
// without and what it merely wants loaded first. The dependency directives are
// what make the load order more than alphabetical: a dependency exists to
// define what the dependant reads at file scope.

#include "catch_amalgamated.hpp"

#include "addons/toc_parser.hpp"

using wowee::addons::parseTocText;

TEST_CASE("a manifest names its addon after the file", "[toc]") {
    const auto toc = parseTocText("Interface/AddOns/MyAddon/MyAddon.toc",
                                  "## Title: My Addon\nMyAddon.lua\n");
    REQUIRE(toc.has_value());
    REQUIRE(toc->addonName == "MyAddon");
    REQUIRE(toc->basePath == "Interface/AddOns/MyAddon");
    REQUIRE(toc->getTitle() == "My Addon");
    REQUIRE(toc->files == std::vector<std::string>{"MyAddon.lua"});
}

TEST_CASE("an archive path names an addon the same way", "[toc]") {
    // Inside the game's own archives there is no file to open and the path is
    // backslash-separated and lowercased; the addon is still named by its
    // folder, and a Blizzard name is restored to its canonical spelling.
    const auto toc = parseTocText(
        "interface\\addons\\blizzard_talentui\\blizzard_talentui.toc",
        "## Interface: 30300\n## LoadOnDemand: 1\nBlizzard_TalentUI.xml\n");
    REQUIRE(toc.has_value());
    REQUIRE(toc->addonName == "Blizzard_TalentUI");
    REQUIRE(toc->basePath == "interface\\addons\\blizzard_talentui");
    REQUIRE(toc->isLoadOnDemand());
    REQUIRE(toc->getInterfaceVersion() == 30300);
}

TEST_CASE("comments and blank lines are not files", "[toc]") {
    const auto toc = parseTocText("A/A.toc",
                                  "## Title: A\n"
                                  "\n"
                                  "# a plain comment\n"
                                  "   \n"
                                  "First.lua\n"
                                  "Sub\\Second.xml\n");
    REQUIRE(toc.has_value());
    // Separators are normalized forward for the filesystem walk.
    REQUIRE(toc->files == std::vector<std::string>{"First.lua", "Sub/Second.xml"});
}

TEST_CASE("required dependencies are read under both spellings", "[toc]") {
    const auto blizzard = parseTocText("A/A.toc", "## Dependencies: Foo, Bar\n");
    REQUIRE(blizzard.has_value());
    REQUIRE(blizzard->getDependencies() == std::vector<std::string>{"Foo", "Bar"});

    const auto thirdParty = parseTocText("A/A.toc", "## RequiredDeps: Baz\n");
    REQUIRE(thirdParty.has_value());
    REQUIRE(thirdParty->getDependencies() == std::vector<std::string>{"Baz"});
}

TEST_CASE("optional dependencies are read under both spellings", "[toc]") {
    const auto shortForm = parseTocText("A/A.toc", "## OptionalDeps: Foo, Bar\n");
    REQUIRE(shortForm.has_value());
    REQUIRE(shortForm->getOptionalDependencies() == std::vector<std::string>{"Foo", "Bar"});

    const auto longForm = parseTocText("A/A.toc", "## OptionalDependencies: Baz\n");
    REQUIRE(longForm.has_value());
    REQUIRE(longForm->getOptionalDependencies() == std::vector<std::string>{"Baz"});
}

TEST_CASE("an addon that states nothing depends on nothing", "[toc]") {
    const auto toc = parseTocText("A/A.toc", "## Title: A\nA.lua\n");
    REQUIRE(toc.has_value());
    REQUIRE(toc->getDependencies().empty());
    REQUIRE(toc->getOptionalDependencies().empty());
    REQUIRE(toc->getInterfaceVersion() == 0);
    REQUIRE_FALSE(toc->isLoadOnDemand());
}

TEST_CASE("saved variables are listed per addon and per character", "[toc]") {
    const auto toc = parseTocText("A/A.toc",
                                  "## SavedVariables: ADb, AOptions\n"
                                  "## SavedVariablesPerCharacter: AChar\n");
    REQUIRE(toc.has_value());
    REQUIRE(toc->getSavedVariables() == std::vector<std::string>{"ADb", "AOptions"});
    REQUIRE(toc->getSavedVariablesPerCharacter() == std::vector<std::string>{"AChar"});
}

TEST_CASE("windows line endings do not become part of a value", "[toc]") {
    const auto toc = parseTocText("A/A.toc",
                                  "## Title: A\r\n## Dependencies: Foo\r\nA.lua\r\n");
    REQUIRE(toc.has_value());
    REQUIRE(toc->getTitle() == "A");
    REQUIRE(toc->getDependencies() == std::vector<std::string>{"Foo"});
    REQUIRE(toc->files == std::vector<std::string>{"A.lua"});
}
