// Paths as the game's archives spell them.
//
// The original interface names its includes relative to the file including
// them and does use "..": the guild bank's XML asks for
// "..\\..\\FrameXML\\UIPanelTemplates.xml". A filesystem walks that itself, an
// archive does not, and AssetManager::normalizePath refuses a path still
// holding "..", so the walk has to happen before the read.

#include "catch_amalgamated.hpp"

#include "pipeline/virtual_path.hpp"

using wowee::pipeline::joinVirtual;
using wowee::pipeline::normalizeVirtual;
using wowee::pipeline::virtualBasename;
using wowee::pipeline::virtualHasExtension;
using wowee::pipeline::virtualParent;

TEST_CASE("separators and case are normalized", "[virtual_path]") {
    REQUIRE(normalizeVirtual("Interface/FrameXML/UI.xml") == "interface\\framexml\\ui.xml");
    REQUIRE(normalizeVirtual("Interface\\FrameXML\\UI.xml") == "interface\\framexml\\ui.xml");
    REQUIRE(normalizeVirtual("INTERFACE/framexml\\Ui.XML") == "interface\\framexml\\ui.xml");
}

TEST_CASE("empty components are dropped", "[virtual_path]") {
    REQUIRE(normalizeVirtual("\\interface\\\\framexml\\") == "interface\\framexml");
    REQUIRE(normalizeVirtual("//interface//framexml//") == "interface\\framexml");
    REQUIRE(normalizeVirtual("").empty());
    REQUIRE(normalizeVirtual("\\\\").empty());
}

TEST_CASE("a single dot names the directory it sits in", "[virtual_path]") {
    REQUIRE(normalizeVirtual("interface\\.\\framexml\\ui.xml") ==
            "interface\\framexml\\ui.xml");
    REQUIRE(normalizeVirtual(".\\ui.xml") == "ui.xml");
}

TEST_CASE("a double dot is applied rather than passed on", "[virtual_path]") {
    REQUIRE(normalizeVirtual("interface\\addons\\..\\framexml\\ui.xml") ==
            "interface\\framexml\\ui.xml");
    // The guild bank's own include, which is why any of this exists.
    REQUIRE(joinVirtual("interface\\addons\\blizzard_guildbankui",
                        "..\\..\\FrameXML\\UIPanelTemplates.xml") ==
            "interface\\framexml\\uipaneltemplates.xml");
}

TEST_CASE("climbing above the root stops at the root", "[virtual_path]") {
    // There is nothing above the data directory to name, and a path that still
    // held ".." would be refused by the asset manager rather than resolved.
    REQUIRE(normalizeVirtual("..\\..\\..\\interface\\ui.xml") == "interface\\ui.xml");
    REQUIRE(normalizeVirtual("interface\\..\\..\\..\\ui.xml") == "ui.xml");
    REQUIRE(normalizeVirtual("..").empty());
    REQUIRE(joinVirtual("interface", "..\\..\\..\\x.lua") == "x.lua");
}

TEST_CASE("parent and basename split a path", "[virtual_path]") {
    REQUIRE(virtualParent("Interface\\FrameXML\\UI.xml") == "interface\\framexml");
    REQUIRE(virtualBasename("Interface\\FrameXML\\UI.xml") == "ui.xml");
    // Something at the root has no parent.
    REQUIRE(virtualParent("ui.xml").empty());
    REQUIRE(virtualBasename("ui.xml") == "ui.xml");
    REQUIRE(virtualParent("").empty());
    REQUIRE(virtualBasename("").empty());
}

TEST_CASE("joining against nothing is just normalizing", "[virtual_path]") {
    REQUIRE(joinVirtual("", "Interface/FrameXML/UI.xml") == "interface\\framexml\\ui.xml");
}

TEST_CASE("a bare name resolves beside the file that asked for it",
          "[virtual_path]") {
    // inspectpvpframe.xml asks for PVPFrameTemplates.xml by bare name.
    REQUIRE(joinVirtual("interface\\framexml", "PVPFrameTemplates.xml") ==
            "interface\\framexml\\pvpframetemplates.xml");
}

TEST_CASE("extensions are matched without regard to case", "[virtual_path]") {
    // FrameXML.toc names files as "Fonts.xml" and addons ship "Foo.LUA".
    REQUIRE(virtualHasExtension("interface\\framexml\\fonts.xml", ".xml"));
    REQUIRE(virtualHasExtension("Interface\\AddOns\\Foo\\Foo.LUA", ".lua"));
    REQUIRE_FALSE(virtualHasExtension("interface\\framexml\\fonts.xml", ".lua"));
    REQUIRE_FALSE(virtualHasExtension("lua", ".lua"));
    REQUIRE(virtualHasExtension("a.lua", ".lua"));
}
