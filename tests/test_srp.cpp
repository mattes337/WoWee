// SRP6a challenge/proof smoke tests
#include <catch_amalgamated.hpp>
#include "auth/srp.hpp"
#include "auth/crypto.hpp"

using wowee::auth::SRP;
using wowee::auth::Crypto;
using wowee::auth::BigNum;

// WoW 3.3.5a uses well-known SRP6a parameters.
// Generator g = 7, N = a large 32-byte safe prime.
// We use the canonical WoW values for integration-level tests.

static const std::vector<uint8_t> kWoWGenerator = { 7 };

// WoW's 32-byte large safe prime (little-endian)
static const std::vector<uint8_t> kWoWPrime = {
    0xB7, 0x9B, 0x3E, 0x2A, 0x87, 0x82, 0x3C, 0xAB,
    0x8F, 0x5E, 0xBF, 0xBF, 0x8E, 0xB1, 0x01, 0x08,
    0x53, 0x50, 0x06, 0x29, 0x8B, 0x5B, 0xAD, 0xBD,
    0x5B, 0x53, 0xE1, 0x89, 0x5E, 0x64, 0x4B, 0x89
};

TEST_CASE("SRP initialize stores credentials", "[srp]") {
    SRP srp;
    // Should not throw
    REQUIRE_NOTHROW(srp.initialize("TEST", "PASSWORD"));
}

TEST_CASE("SRP initializeWithHash accepts pre-computed hash", "[srp]") {
    // Pre-compute SHA1("TEST:PASSWORD")
    auto hash = Crypto::sha1(std::string("TEST:PASSWORD"));
    REQUIRE(hash.size() == 20);

    SRP srp;
    REQUIRE_NOTHROW(srp.initializeWithHash("TEST", hash));
}

TEST_CASE("SRP feed produces A and M1 of correct sizes", "[srp]") {
    SRP srp;
    srp.initialize("TEST", "PASSWORD");

    // Fabricate a server B (32 bytes, non-zero to avoid SRP abort)
    std::vector<uint8_t> B(32, 0);
    B[0] = 0x42; // Non-zero

    std::vector<uint8_t> salt(32, 0xAA);

    srp.feed(B, kWoWGenerator, kWoWPrime, salt);

    auto A = srp.getA();
    auto M1 = srp.getM1();
    auto K = srp.getSessionKey();

    // A should be 32 bytes (same size as N)
    REQUIRE(A.size() == 32);
    // M1 is SHA1 → 20 bytes
    REQUIRE(M1.size() == 20);
    // K is the interleaved session key → 40 bytes
    REQUIRE(K.size() == 40);
}

TEST_CASE("SRP A is non-zero", "[srp]") {
    SRP srp;
    srp.initialize("PLAYER", "SECRET");

    std::vector<uint8_t> B(32, 0);
    B[3] = 0x01;
    std::vector<uint8_t> salt(32, 0xBB);

    srp.feed(B, kWoWGenerator, kWoWPrime, salt);

    auto A = srp.getA();
    bool allZero = true;
    for (auto b : A) {
        if (b != 0) { allZero = false; break; }
    }
    REQUIRE_FALSE(allZero);
}

TEST_CASE("SRP different passwords produce different M1", "[srp]") {
    auto runSrp = [](const std::string& pass) {
        SRP srp;
        srp.initialize("TESTUSER", pass);
        std::vector<uint8_t> B(32, 0);
        B[0] = 0x11;
        std::vector<uint8_t> salt(32, 0xCC);
        srp.feed(B, kWoWGenerator, kWoWPrime, salt);
        return srp.getM1();
    };

    auto m1a = runSrp("PASSWORD1");
    auto m1b = runSrp("PASSWORD2");
    REQUIRE(m1a != m1b);
}

TEST_CASE("SRP verifyServerProof rejects wrong proof", "[srp]") {
    SRP srp;
    srp.initialize("TEST", "PASSWORD");

    std::vector<uint8_t> B(32, 0);
    B[0] = 0x55;
    std::vector<uint8_t> salt(32, 0xDD);

    srp.feed(B, kWoWGenerator, kWoWPrime, salt);

    // Random 20 bytes should not match the expected M2
    std::vector<uint8_t> fakeM2(20, 0xFF);
    REQUIRE_FALSE(srp.verifyServerProof(fakeM2));
}

TEST_CASE("SRP setUseHashedK changes behavior", "[srp]") {
    auto runWithHashedK = [](bool useHashed) {
        SRP srp;
        srp.setUseHashedK(useHashed);
        srp.initialize("TEST", "PASSWORD");
        std::vector<uint8_t> B(32, 0);
        B[0] = 0x22;
        std::vector<uint8_t> salt(32, 0xEE);
        srp.feed(B, kWoWGenerator, kWoWPrime, salt);
        return srp.getM1();
    };

    auto m1_default = runWithHashedK(false);
    auto m1_hashed = runWithHashedK(true);
    // Different k derivation → different M1
    REQUIRE(m1_default != m1_hashed);
}

TEST_CASE("SRP proof agrees with server arithmetic for zero-padded challenge fields", "[srp]") {
    // Synthetic registration only. b=147 makes B's high little-endian byte zero;
    // the salt deliberately has the same padding edge. No private client
    // ephemeral injection is needed: derive the server secret from emitted A.
    const auto hashParts = [](std::initializer_list<std::vector<uint8_t>> parts) {
        std::vector<uint8_t> joined;
        for (const auto& part : parts) joined.insert(joined.end(), part.begin(), part.end());
        return Crypto::sha1(joined);
    };
    std::vector<uint8_t> salt(32, 0xAA);
    salt.back() = 0;
    const BigNum modulus(kWoWPrime, true), generator(7), serverPrivate(147);
    const BigNum exponent(hashParts({salt, Crypto::sha1(std::string("TEST:PASSWORD"))}), true);
    const BigNum verifier = generator.modPow(exponent, modulus);
    const BigNum serverPublic = verifier.multiply(BigNum(3))
        .add(generator.modPow(serverPrivate, modulus)).mod(modulus);
    REQUIRE(serverPublic.toArray(true).size() == 31);
    const auto publicBytes = serverPublic.toArray(true, 32);

    SRP client;
    client.initialize("TEST", "PASSWORD");
    client.feed(publicBytes, kWoWGenerator, kWoWPrime, salt);
    const auto clientPublic = client.getA();
    const BigNum scrambler(hashParts({clientPublic, publicBytes}), true);
    const auto shared = BigNum(clientPublic, true).multiply(verifier.modPow(scrambler, modulus))
        .modPow(serverPrivate, modulus).toArray(true, 32);
    size_t start = 0;
    while (start < shared.size() && shared[start] == 0) ++start;
    start += start % 2;
    std::vector<uint8_t> even, odd;
    for (size_t i = start; i < shared.size(); i += 2) {
        even.push_back(shared[i]);
        odd.push_back(shared[i + 1]);
    }
    const auto evenHash = Crypto::sha1(even), oddHash = Crypto::sha1(odd);
    std::vector<uint8_t> key;
    for (size_t i = 0; i < 20; ++i) {
        key.push_back(evenHash[i]);
        key.push_back(oddHash[i]);
    }
    REQUIRE(client.getSessionKey() == key);
    auto groupHash = Crypto::sha1(kWoWPrime);
    const auto generatorHash = Crypto::sha1(kWoWGenerator);
    for (size_t i = 0; i < groupHash.size(); ++i) groupHash[i] ^= generatorHash[i];
    const auto proof = hashParts({groupHash, Crypto::sha1(std::string("TEST")), salt,
                                 clientPublic, publicBytes, key});
    REQUIRE(client.getM1() == proof);
    REQUIRE(client.verifyServerProof(hashParts({clientPublic, proof, key})));
}
