// The reduced terrain index sets, and the promise that a chunk drawn with one
// still closes.
//
// A LOD level that drops vertices changes where the ground is between the ones
// it keeps, and the chunk next to it did not. What the player sees through the
// disagreement is the sky, in a thin flickering line along a chunk boundary -
// the classic terrain crack. The skirt is what closes it, and these are the
// properties that make the skirt work: it hangs from every vertex of the outer
// ring at every level, it is a closed band, and the surface it hangs from uses
// only vertices the neighbouring chunk also has.
//
// None of this needs a GPU or an ADT: the index sets are pure functions of the
// level, which is the whole reason they were written as one.

#include "pipeline/terrain_mesh.hpp"

#include <catch_amalgamated.hpp>

#include <map>
#include <set>
#include <utility>

using wowee::pipeline::kChunkGridVertices;
using wowee::pipeline::kChunkSkirtVertices;
using wowee::pipeline::kChunkVertices;
using wowee::pipeline::kTerrainLodLevels;
using wowee::pipeline::terrainLodForDistance;
using wowee::pipeline::terrainLodIndices;

namespace {

/// An edge, with its two ends put in a fixed order so the same edge walked
/// from either triangle is the same key.
std::pair<uint32_t, uint32_t> edgeKey(uint32_t a, uint32_t b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
}

}  // namespace

TEST_CASE("level 0 has no shared index set", "[terrain][lod]") {
    // Full detail is the chunk's own buffer, because only that one knows where
    // the chunk's holes are.
    REQUIRE(terrainLodIndices(0).indices.empty());
}

TEST_CASE("each level keeps the vertex count it promises", "[terrain][lod]") {
    // 81, 25 and 9 of the 9x9 outer grid - every second, fourth and eighth.
    const int expectedGrid[] = {0, 81, 25, 9};
    for (int level = 1; level < kTerrainLodLevels; ++level) {
        const auto& set = terrainLodIndices(level);
        std::set<uint32_t> used;
        for (uint32_t i = 0; i < set.surfaceIndexCount; ++i) {
            used.insert(set.indices[i]);
        }
        INFO("level " << level);
        CHECK(static_cast<int>(used.size()) == expectedGrid[level]);
        // Every one of them is an outer-grid vertex: row * 17 + column with
        // the column no further than 8, which is what the neighbouring chunk
        // shares. An inner vertex would be one this chunk has and the chunk
        // beside it does not.
        for (uint32_t v : used) {
            CHECK(v < static_cast<uint32_t>(kChunkGridVertices));
            CHECK(v % 17 <= 8);
        }
    }
}

TEST_CASE("no index reaches past the chunk's vertices", "[terrain][lod]") {
    for (int level = 1; level < kTerrainLodLevels; ++level) {
        const auto& set = terrainLodIndices(level);
        REQUIRE_FALSE(set.indices.empty());
        REQUIRE(set.indices.size() % 3 == 0);
        for (uint32_t v : set.indices) {
            INFO("level " << level << " index " << v);
            REQUIRE(v < static_cast<uint32_t>(kChunkVertices));
        }
    }
}

TEST_CASE("the surface of every level is watertight", "[terrain][lod]") {
    // Watertight here means what it means for a closed strip of triangles: an
    // interior edge is walked by exactly two of them, and a boundary edge by
    // exactly one. An edge walked once in the middle of the sheet is a crack;
    // an edge walked three times is a fold.
    for (int level = 1; level < kTerrainLodLevels; ++level) {
        const auto& set = terrainLodIndices(level);
        std::map<std::pair<uint32_t, uint32_t>, int> edges;
        for (uint32_t i = 0; i < set.surfaceIndexCount; i += 3) {
            const uint32_t a = set.indices[i];
            const uint32_t b = set.indices[i + 1];
            const uint32_t c = set.indices[i + 2];
            REQUIRE(a != b);
            REQUIRE(b != c);
            REQUIRE(a != c);
            edges[edgeKey(a, b)]++;
            edges[edgeKey(b, c)]++;
            edges[edgeKey(c, a)]++;
        }
        int boundary = 0;
        for (const auto& [edge, count] : edges) {
            INFO("level " << level << " edge " << edge.first << "-" << edge.second
                          << " walked " << count << " times");
            REQUIRE(count <= 2);
            if (count == 1) ++boundary;
        }
        // The boundary of a square sheet of n-by-n cells is 4n edges, and the
        // diagonal each quad is split along is interior. Level 1 is 8 cells a
        // side, level 2 is 4, level 3 is 2.
        const int cells = 8 >> (level - 1);
        INFO("level " << level);
        CHECK(boundary == 4 * cells);
    }
}

TEST_CASE("the skirt is a closed band around the whole ring", "[terrain][lod]") {
    for (int level = 1; level < kTerrainLodLevels; ++level) {
        const auto& set = terrainLodIndices(level);
        const size_t skirtStart = set.surfaceIndexCount;
        REQUIRE(set.indices.size() > skirtStart);
        // Two triangles per ring segment, and there are as many segments as
        // ring vertices because the band closes.
        CHECK(set.indices.size() - skirtStart ==
              static_cast<size_t>(kChunkSkirtVertices) * 6);

        std::set<uint32_t> skirtVerts;
        std::set<uint32_t> ringVerts;
        for (size_t i = skirtStart; i < set.indices.size(); ++i) {
            const uint32_t v = set.indices[i];
            if (v >= static_cast<uint32_t>(kChunkGridVertices)) {
                skirtVerts.insert(v);
            } else {
                ringVerts.insert(v);
            }
        }
        INFO("level " << level);
        // Every hanging vertex is used, and every vertex of the outer ring is
        // hung from - including the ones this level dropped from its surface,
        // because the neighbour still has them.
        CHECK(skirtVerts.size() == static_cast<size_t>(kChunkSkirtVertices));
        CHECK(ringVerts.size() == static_cast<size_t>(kChunkSkirtVertices));
        for (uint32_t v : ringVerts) {
            const uint32_t row = v / 17;
            const uint32_t col = v % 17;
            CHECK(col <= 8);
            CHECK((row == 0 || row == 8 || col == 0 || col == 8));
        }
    }
}

TEST_CASE("neighbouring chunks are never more than one level apart",
          "[terrain][lod]") {
    // Two chunks that touch are 33.3 yards apart, and at the worst angle their
    // distances from the camera differ by at most the diagonal, 47 yards. The
    // thresholds are 12, 30 and 60 percent of the view distance, so the
    // narrowest gap between two of them is 18 percent - 72 yards at the
    // shortest view distance the setting allows. No pair of neighbours can
    // straddle two thresholds, which is what a skirt is sized for.
    constexpr float kNeighbourSpread = 47.2f;
    for (float viewDistance : {400.0f, 1000.0f, 1600.0f, 2400.0f}) {
        for (float d = 0.0f; d < viewDistance; d += 3.0f) {
            const int here = terrainLodForDistance(d, viewDistance, 3);
            const int there = terrainLodForDistance(d + kNeighbourSpread, viewDistance, 3);
            INFO("view " << viewDistance << " at " << d);
            REQUIRE(there - here <= 1);
            REQUIRE(there >= here);
        }
    }
}

TEST_CASE("Off draws everything at full detail", "[terrain][lod]") {
    for (float d = 0.0f; d < 2400.0f; d += 25.0f) {
        REQUIRE(terrainLodForDistance(d, 1200.0f, 0) == 0);
    }
    // And the setting caps the level rather than shifting the thresholds, so
    // Near never reaches past level 1.
    for (float d = 0.0f; d < 1200.0f; d += 25.0f) {
        REQUIRE(terrainLodForDistance(d, 1200.0f, 1) <= 1);
    }
}
