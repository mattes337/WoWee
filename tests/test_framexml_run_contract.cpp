#include <catch_amalgamated.hpp>
#include "addons/framexml_run_contract.hpp"
#include <limits>
#include <chrono>
#include <filesystem>

using namespace wowee::addons;

TEST_CASE("runner viewport validates both dimensions before changing setup", "[framexml-contract]") {
    RunnerViewport viewport;
    CHECK(parseRunnerViewport("1024x768", viewport));
    CHECK(viewport.width == 1024);
    CHECK(viewport.height == 768);
    for (const auto invalid : {"", "1024", "x768", "1024x", "0x768", "1024x0",
                              "-1x768", "+1x768", "1024x768junk", "1024x768x1",
                              " 1024x768", "16385x768", "1024x999999999999999999"}) {
        CAPTURE(invalid);
        CHECK_FALSE(parseRunnerViewport(invalid, viewport));
        CHECK(viewport.width == 1024);
        CHECK(viewport.height == 768);
    }
    CHECK(parseRunnerViewport("1x16384", viewport));
    CHECK(viewport.width == 1);
    CHECK(viewport.height == 16384);
}

TEST_CASE("runner hit coordinates require two complete finite numbers", "[framexml-contract]") {
    RunnerPoint point{7.0f, 8.0f};
    CHECK(parseRunnerPoint("-12.5,34", point));
    CHECK(point.x == -12.5f);
    CHECK(point.y == 34.0f);
    for (const auto invalid : {"", "1", ",2", "1,", "1,2,3", "1,2junk",
                               " 1,2", "1, 2", "nan,2", "1,NaN", "inf,2",
                               "1,-inf", "1e999,2"}) {
        CAPTURE(invalid);
        CHECK_FALSE(parseRunnerPoint(invalid, point));
        CHECK(point.x == -12.5f);
        CHECK(point.y == 34.0f);
    }
}

TEST_CASE("runner mouse requires complete coordinates and recognized buttons",
          "[framexml-contract]") {
    RunnerMouse mouse{{7.0f, 8.0f}, "L"};
    CHECK(parseRunnerMouse("-3.25,4.5,", mouse));
    CHECK(mouse.point.x == -3.25f);
    CHECK(mouse.point.y == 4.5f);
    CHECK(mouse.buttons.empty());
    for (const auto valid : {"1,2,L", "1,2,R", "1,2,M", "1,2,LRM", "1,2,MR"}) {
        CAPTURE(valid);
        CHECK(parseRunnerMouse(valid, mouse));
    }
    const RunnerMouse retained = mouse;
    for (const auto invalid : {"", "1,2", "1,2,X", "1,2,left", "1,2,Ljunk",
                               "1,2,L,", "nan,2,L", "1,inf,R", "1x,2,L"}) {
        CAPTURE(invalid);
        CHECK_FALSE(parseRunnerMouse(invalid, mouse));
        CHECK(mouse.point.x == retained.point.x);
        CHECK(mouse.point.y == retained.point.y);
        CHECK(mouse.buttons == retained.buttons);
    }
}

TEST_CASE("FrameXML runner failures in setup and callbacks cannot produce green", "[framexml-contract]") {
    CHECK(frameXmlRunExitCode(true, true, 0, 0, 0) == 0);
    CHECK(frameXmlRunExitCode(false, true, 0, 0, 0) != 0);
    CHECK(frameXmlRunExitCode(true, false, 0, 0, 0) != 0);
    CHECK(frameXmlRunExitCode(true, true, 1, 0, 0) != 0);
    CHECK(frameXmlRunExitCode(true, true, 0, 1, 0) != 0);
    CHECK(frameXmlRunExitCode(true, true, 0, 0, 1) != 0);
    CHECK(frameXmlRunExitCode(false, false, std::numeric_limits<std::size_t>::max(), 100, 100) == 100);
}

TEST_CASE("empty runner expressions and scripts are invalid", "[framexml-contract]") {
    CHECK_FALSE(hasLuaExpression(""));
    CHECK_FALSE(hasLuaExpression(" \t\r\n"));
    CHECK(hasLuaExpression("assert(true)"));
    CHECK(hasLuaExpression("\n-- explicit no-op script\n"));
}

TEST_CASE("requested runner scripts must exist and contain code", "[framexml-contract]") {
    const auto name = "wowee-framexml-contract-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ".lua";
    const auto path = std::filesystem::temp_directory_path() / name;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
    } cleanup{path};
    CHECK_FALSE(readableRunnerScript(path.string()));
    { std::ofstream file(path); file << " \n\t"; }
    CHECK_FALSE(readableRunnerScript(path.string()));
    { std::ofstream file(path); file << "assert(true)\n"; }
    CHECK(readableRunnerScript(path.string()));
}
