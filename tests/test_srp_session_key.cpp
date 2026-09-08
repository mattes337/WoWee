#include <catch_amalgamated.hpp>
#include "auth/srp_session_key.hpp"
#include <string>

namespace {
std::vector<uint8_t> fromHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}
}

TEST_CASE("WoW SRP session key matches independent SHA1 interleave vectors", "[srp]") {
    // Fixed Python hashlib results, independent of Crypto and this helper.
    // See docs/evidence/srp-session-key-vectors.md for the reproduction script.
    struct Vector { const char* secret; const char* expected; };
    const Vector vectors[] = {
        {"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
         "ed2976c475109d044df3443c9cc86c31077e9c7db8e04d3d699f68bd6bcc6c6b20df81f6b7d66fa0"},
        {"0002030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
         "324ee42a4dc3379278dea177486fba3715c1d9173be0101572fd5f0cc9b6708075b5b8928c829c52"},
        {"0000030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
         "324ee42a4dc3379278dea177486fba3715c1d9173be0101572fd5f0cc9b6708075b5b8928c829c52"},
        {"0000000405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20",
         "249481298dd8dc043490ee550d33ace31ca8ba9aa8a8e322fdeb1c6f2c27e8622184c4131a7916b1"},
        {"0000000000000000000000000000000000000000000000000000000000000000",
         "dada3939a3a3eeee5e5e6b6b4b4b0d0d32325555bfbfefef9595606018189090afafd8d807070909"},
        {"0030bbc02ca2a089aefaae951b33cf344c0fddf79bf1c4527f022a2803402e46",
         "1421793e778fe54d7e75e8c42793fd0b7b547a1cd6cb16904d76433003ef93705012e6fd901ac590"},
    };
    for (const auto& v : vectors) {
        INFO("synthetic secret " << v.secret);
        REQUIRE(wowee::auth::deriveSrpSessionKey(fromHex(v.secret)) == fromHex(v.expected));
    }
}

TEST_CASE("WoW SRP interleave rejects non-wire-sized secrets", "[srp]") {
    for (const auto size : {0u, 1u, 31u, 33u, 64u}) {
        REQUIRE_THROWS_AS(wowee::auth::deriveSrpSessionKey(std::vector<uint8_t>(size)),
                          std::invalid_argument);
    }
}
