// The two pure halves of NormalMapCache: the name a map gets on disk, and the
// box filter that puts an oversized source under the cap before the Sobel runs.
//
// Both are what the cache is *correct* about rather than what it is fast about,
// and both are unreachable from a renderer: the rest of the class needs a Vulkan
// context, a thread pool and a file system. They are inline in the header for
// exactly this reason.
//
// What each of them being wrong would look like from the chair:
//
//   * a hash that does not depend on the strength gives a doodad the ground's
//     gentler map, or the other way round, and nothing ever says so;
//   * a hash that does not depend on the bytes serves a stale map for a texture
//     that has been replaced, forever, because nothing else names the file;
//   * a downsample that reads out of bounds is a crash on the first 1024x1024
//     texture in a zone, on a worker thread, with no useful stack.

#include <catch_amalgamated.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "rendering/normal_map_cache.hpp"

using wowee::rendering::NormalMapCache;

namespace {

/// A grey ramp, so a box filter has something to average that is not constant.
std::vector<uint8_t> ramp(uint32_t w, uint32_t h) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t v = static_cast<uint8_t>((x * 7 + y * 13) & 0xFF);
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            px[i + 0] = v;
            px[i + 1] = v;
            px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return px;
}

}  // namespace

TEST_CASE("the same bytes at the same strength always name the same file", "[normalmapcache]") {
    const std::vector<uint8_t> a = ramp(8, 8);
    const std::vector<uint8_t> b = a;

    CHECK(NormalMapCache::hashName(a.data(), a.size(), 3.0f) ==
          NormalMapCache::hashName(b.data(), b.size(), 3.0f));

    INFO("a name is sixteen hex digits, so it is a legal file name everywhere");
    const std::string name = NormalMapCache::hashName(a.data(), a.size(), 3.0f);
    CHECK(name.size() == 16);
    for (char c : name) {
        CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
    }
}

TEST_CASE("the strength is part of the name", "[normalmapcache]") {
    // A doodad's map is derived at 3 and a tileset's at 2. They are two
    // different maps of the same pixels, and a name that ignored the strength
    // would hand whichever was made first to both.
    const std::vector<uint8_t> px = ramp(8, 8);
    CHECK(NormalMapCache::hashName(px.data(), px.size(), 2.0f) !=
          NormalMapCache::hashName(px.data(), px.size(), 3.0f));
}

TEST_CASE("a changed texture gets a new name rather than a stale map", "[normalmapcache]") {
    std::vector<uint8_t> px = ramp(8, 8);
    const std::string before = NormalMapCache::hashName(px.data(), px.size(), 3.0f);
    px[17] = static_cast<uint8_t>(px[17] ^ 0x01);  // one bit, in the middle
    CHECK(NormalMapCache::hashName(px.data(), px.size(), 3.0f) != before);

    INFO("and so does a texture that is merely longer");
    px.push_back(0);
    CHECK(NormalMapCache::hashName(px.data(), px.size(), 3.0f) != before);
}

TEST_CASE("a source already under the cap is handed back untouched", "[normalmapcache]") {
    const uint32_t w = NormalMapCache::kMaxSourceSide;
    const uint32_t h = 64;
    const std::vector<uint8_t> px = ramp(w, h);
    uint32_t ow = 0, oh = 0;
    const std::vector<uint8_t> out = NormalMapCache::downsampleToCap(px, w, h, ow, oh);
    CHECK(ow == w);
    CHECK(oh == h);
    CHECK(out == px);
}

TEST_CASE("an oversized source is halved until both axes fit", "[normalmapcache]") {
    const uint32_t w = NormalMapCache::kMaxSourceSide * 4;
    const uint32_t h = NormalMapCache::kMaxSourceSide * 2;
    const std::vector<uint8_t> px = ramp(w, h);
    uint32_t ow = 0, oh = 0;
    const std::vector<uint8_t> out = NormalMapCache::downsampleToCap(px, w, h, ow, oh);

    CHECK(ow <= NormalMapCache::kMaxSourceSide);
    CHECK(oh <= NormalMapCache::kMaxSourceSide);
    // Halving, not resampling: the aspect ratio survives.
    CHECK(ow == NormalMapCache::kMaxSourceSide);
    CHECK(oh == NormalMapCache::kMaxSourceSide / 2);
    CHECK(out.size() == static_cast<size_t>(ow) * oh * 4);
}

TEST_CASE("the halving is the average of the four texels it replaces", "[normalmapcache]") {
    // 1024 so one halving is enough, and a pattern whose averages are exact.
    const uint32_t w = NormalMapCache::kMaxSourceSide * 2;
    const uint32_t h = NormalMapCache::kMaxSourceSide * 2;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
    // The top-left 2x2 block holds 0, 4, 8 and 12: mean 6.
    const uint8_t block[4] = {0, 4, 8, 12};
    px[0] = block[0];
    px[4] = block[1];
    px[static_cast<size_t>(w) * 4] = block[2];
    px[static_cast<size_t>(w) * 4 + 4] = block[3];

    uint32_t ow = 0, oh = 0;
    const std::vector<uint8_t> out = NormalMapCache::downsampleToCap(px, w, h, ow, oh);
    REQUIRE(ow == NormalMapCache::kMaxSourceSide);
    REQUIRE(oh == NormalMapCache::kMaxSourceSide);
    CHECK(static_cast<int>(out[0]) == 6);
}

TEST_CASE("a source that is not the size it claims is refused", "[normalmapcache]") {
    // The worker reads width*height*4 bytes. A BLP whose decode came back short
    // must not be read past its end on a thread with no stack worth printing.
    uint32_t ow = 0, oh = 0;
    const std::vector<uint8_t> tooSmall(16, 0);
    CHECK(NormalMapCache::downsampleToCap(tooSmall, 1024, 1024, ow, oh).empty());
    CHECK(NormalMapCache::downsampleToCap({}, 0, 8, ow, oh).empty());
    CHECK(NormalMapCache::downsampleToCap({}, 8, 0, ow, oh).empty());
}

TEST_CASE("the sidecar header is eight bytes and says what it holds", "[normalmapcache]") {
    // The file is this header and then width*height*4 bytes. Sixteen-bit
    // dimensions are enough because the cap is 512, and the variance is what
    // decides whether the map is worth binding at all.
    CHECK(sizeof(NormalMapCache::SidecarHeader) == 8);
    NormalMapCache::SidecarHeader header{512, 256, 0.25f};
    CHECK(header.width == 512);
    CHECK(header.height == 256);
    CHECK(header.variance == 0.25f);
}
