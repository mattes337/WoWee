#pragma once

#include "auth/crypto.hpp"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace wowee::auth {

// WoW SRP SHA1Interleave uses the fixed-width little-endian secret. Leading
// zero bytes are removed in pairs before the alternating bytes are hashed.
inline std::vector<uint8_t> deriveSrpSessionKey(
    const std::vector<uint8_t>& secretLittleEndian) {
    if (secretLittleEndian.size() != 32) {
        throw std::invalid_argument("SRP session secret must contain exactly 32 bytes");
    }
    std::size_t first = 0;
    while (first < secretLittleEndian.size() && secretLittleEndian[first] == 0) {
        ++first;
    }
    first += first % 2;

    std::vector<uint8_t> even;
    std::vector<uint8_t> odd;
    even.reserve(16);
    odd.reserve(16);
    for (std::size_t i = first; i < secretLittleEndian.size(); i += 2) {
        even.push_back(secretLittleEndian[i]);
        odd.push_back(secretLittleEndian[i + 1]);
    }
    const auto evenHash = Crypto::sha1(even);
    const auto oddHash = Crypto::sha1(odd);
    std::vector<uint8_t> key;
    key.reserve(40);
    for (std::size_t i = 0; i < 20; ++i) {
        key.push_back(evenHash[i]);
        key.push_back(oddHash[i]);
    }
    return key;
}

} // namespace wowee::auth
