// $parent in the name handed to CreateFrame, CreateTexture, CreateFontString.
//
// The XML path resolved this token and the Lua path did not, so a frame
// FrameXML built by hand was published under the literal "$parentFoo".
// Nothing raised: the frame existed, GetName() answered the token, and the
// failure surfaced one file later as an index of nil - securitymatrix.lua
// reading _G["SecurityMatrixFrameElementSparkle1_1Highlight"], which nothing
// had ever created because the parent it would have been named after was
// called "$parentElementSparkle1_1".
//
// The half that needs a Lua state - finding the owner the caller passed - is
// not here. The rule about names is, and it is the half with the edge cases.

#include <catch_amalgamated.hpp>

#include "addons/lua_engine.hpp"

#include <string>

using wowee::addons::resolveOwnedName;

TEST_CASE("$parent is replaced by the owner's name", "[parentname]") {
    std::string out;
    REQUIRE(resolveOwnedName("$parentChild", "ProbeParent", out));
    CHECK(out == "ProbeParentChild");
}

TEST_CASE("a name without the token is left alone", "[parentname]") {
    std::string out;
    REQUIRE(resolveOwnedName("ProbePlain", "ProbeParent", out));
    CHECK(out == "ProbePlain");
    // Only a leading token counts. "Foo$parent" is not a thing FrameXML
    // writes, and treating it as one would rename a frame that asked for
    // exactly what it said.
    REQUIRE(resolveOwnedName("Foo$parentBar", "ProbeParent", out));
    CHECK(out == "Foo$parentBar");
}

TEST_CASE("an unnamed owner lends no name", "[parentname]") {
    // Not the bare suffix. A template replayed onto a series of unnamed frames
    // would publish "Portrait" once per frame, each over the last, and the
    // first thing to read it by name would get somebody else's texture.
    std::string out = "untouched";
    CHECK_FALSE(resolveOwnedName("$parentPortrait", "", out));
}

TEST_CASE("no name asked for is no name given", "[parentname]") {
    // CreateFrame's name argument is optional and CreateTexture's defaults to
    // the empty string, so both arrive here.
    std::string out;
    CHECK_FALSE(resolveOwnedName(nullptr, "ProbeParent", out));
    CHECK_FALSE(resolveOwnedName("", "ProbeParent", out));
}

TEST_CASE("the token alone takes the owner's name whole", "[parentname]") {
    // FrameXML does write a bare "$parent" - a region named after the frame
    // it belongs to and nothing more.
    std::string out;
    REQUIRE(resolveOwnedName("$parent", "ProbeParent", out));
    CHECK(out == "ProbeParent");
}

TEST_CASE("a name shorter than the token is not read past its end",
          "[parentname]") {
    std::string out;
    REQUIRE(resolveOwnedName("$p", "ProbeParent", out));
    CHECK(out == "$p");
}
