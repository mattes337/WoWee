#include <catch_amalgamated.hpp>
#include "addons/framexml_run_contract.hpp"
#include <limits>
#include <chrono>
#include <filesystem>

using namespace wowee::addons;

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
