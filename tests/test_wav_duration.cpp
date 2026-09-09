// How long a WAV runs, read out of its own header.
//
// This client's mixer has no looping one-shot, so a sound that must run
// continuously is re-triggered - and the interval it is re-triggered at is
// this number. Get it short and the track plays over itself; get it long and
// there is a hole of silence under the screen. Neither is audible as "the
// length was computed wrongly", and on a machine with no sound device neither
// is audible at all, which is what makes this the half worth testing.

#include "catch_amalgamated.hpp"
#include "audio/ambient_sound_manager.hpp"

#include <cstdint>
#include <string>
#include <vector>

using wowee::audio::wavDurationSeconds;

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void putTag(std::vector<uint8_t>& out, const char* tag) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(tag[i]));
}

/// A WAV header describing `seconds` of 16-bit stereo at 44.1 kHz, with the
/// sample bytes actually present unless `truncate` says otherwise. `extra` is
/// a chunk wedged between fmt and data, which is where the offset-36 shortcut
/// comes unstuck.
std::vector<uint8_t> makeWav(float seconds, bool byteRateStated = true,
                             size_t extraChunkBytes = 0, bool truncate = false) {
    constexpr uint32_t kSampleRate = 44100;
    constexpr uint16_t kChannels = 2;
    constexpr uint16_t kBits = 16;
    const uint32_t byteRate = kSampleRate * kChannels * (kBits / 8);
    const uint32_t dataBytes = static_cast<uint32_t>(seconds * static_cast<float>(byteRate));

    std::vector<uint8_t> wav;
    putTag(wav, "RIFF");
    put32(wav, 0);                     // size, which nothing here reads
    putTag(wav, "WAVE");

    putTag(wav, "fmt ");
    put32(wav, 16);
    put16(wav, 1);                     // PCM
    put16(wav, kChannels);
    put32(wav, kSampleRate);
    put32(wav, byteRateStated ? byteRate : 0);
    put16(wav, kChannels * (kBits / 8));
    put16(wav, kBits);

    if (extraChunkBytes > 0) {
        putTag(wav, "LIST");
        put32(wav, static_cast<uint32_t>(extraChunkBytes));
        for (size_t i = 0; i < extraChunkBytes; ++i) wav.push_back(0x20);
    }

    putTag(wav, "data");
    put32(wav, dataBytes);
    const uint32_t present = truncate ? dataBytes / 2 : dataBytes;
    wav.insert(wav.end(), present, 0);
    return wav;
}

}  // namespace

TEST_CASE("a plain WAV is as long as its data chunk says") {
    CHECK(wavDurationSeconds(makeWav(3.0f)) == Catch::Approx(3.0f).margin(0.001));
    CHECK(wavDurationSeconds(makeWav(0.25f)) == Catch::Approx(0.25f).margin(0.001));
}

TEST_CASE("a chunk between fmt and data does not move the length") {
    // Reading the chunk that happens to sit at offset 36 - which is where data
    // is in the simplest possible WAV and nowhere else - gives a length that
    // is whatever the intervening chunk holds. The archives carry WAVs written
    // by more than one tool.
    CHECK(wavDurationSeconds(makeWav(3.0f, true, 40)) ==
          Catch::Approx(3.0f).margin(0.001));
}

TEST_CASE("a byte rate the encoder left blank is worked out from the rest") {
    CHECK(wavDurationSeconds(makeWav(2.0f, false)) == Catch::Approx(2.0f).margin(0.001));
}

TEST_CASE("a file shorter than its header claims is measured by what is there") {
    // A truncated read would otherwise be re-triggered at the length the file
    // says rather than the length it has, leaving a silence exactly as long as
    // the missing half.
    CHECK(wavDurationSeconds(makeWav(4.0f, true, 0, true)) ==
          Catch::Approx(2.0f).margin(0.001));
}

TEST_CASE("bytes that are not a WAV have no length") {
    // Zero is the caller's signal to play the sound once rather than loop it,
    // so this must not be a guess: an mp3 measured as a WAV would be looped on
    // a made-up interval.
    CHECK(wavDurationSeconds({}) == 0.0f);
    CHECK(wavDurationSeconds(std::vector<uint8_t>(200, 0)) == 0.0f);

    SECTION("an mp3, which SoundEntries lists among its wavs") {
        std::vector<uint8_t> mp3{0xFF, 0xFB, 0x90, 0x64};
        mp3.resize(4096, 0);
        CHECK(wavDurationSeconds(mp3) == 0.0f);
    }

    SECTION("a RIFF that is not a WAVE") {
        std::vector<uint8_t> riff = makeWav(1.0f);
        riff[8] = 'A';
        CHECK(wavDurationSeconds(riff) == 0.0f);
    }

    SECTION("a header with no data chunk") {
        std::vector<uint8_t> header = makeWav(1.0f);
        header.resize(36);
        CHECK(wavDurationSeconds(header) == 0.0f);
    }
}

TEST_CASE("a zero-length chunk does not walk the file forever") {
    // A chunk whose size is zero advances the walk by eight bytes, which is
    // fine - but one written at the end of the file with a size of zero used
    // to be a loop that never moved. This test is here because the loop it
    // guards has no other symptom: the client simply stops.
    std::vector<uint8_t> wav;
    putTag(wav, "RIFF");
    put32(wav, 0);
    putTag(wav, "WAVE");
    putTag(wav, "fmt ");
    put32(wav, 16);
    put16(wav, 1);
    put16(wav, 2);
    put32(wav, 44100);
    put32(wav, 44100 * 4);
    put16(wav, 4);
    put16(wav, 16);
    // No data chunk, and a chunk that claims no length at all after it.
    putTag(wav, "junk");
    put32(wav, 0);
    wav.resize(64, 0);
    CHECK(wavDurationSeconds(wav) == 0.0f);
}
