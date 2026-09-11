#include "pipeline/terrain_mesh.hpp"
#include "pipeline/adt_alpha.hpp"
#include <algorithm>
#include "core/coordinates.hpp"
#include "core/logger.hpp"
#include "rendering/tangent_frame.hpp"
#include <cmath>
#include <array>
#include <utility>

namespace wowee {
namespace pipeline {

namespace {

/// The 9x9 outer grid's perimeter, walked once clockwise from the corner at
/// (row 0, column 0). Both the skirt vertices and the skirt triangles are in
/// this order, so skirt vertex k hangs from ring vertex k and the band between
/// k and k+1 closes.
const std::array<std::pair<int, int>, kChunkSkirtVertices>& skirtRing() {
    static const std::array<std::pair<int, int>, kChunkSkirtVertices> ring = [] {
        std::array<std::pair<int, int>, kChunkSkirtVertices> r{};
        int n = 0;
        for (int c = 0; c <= 8; ++c) r[n++] = {0, c};   // top edge, west to east
        for (int rw = 1; rw <= 8; ++rw) r[n++] = {rw, 8};  // east edge
        for (int c = 7; c >= 0; --c) r[n++] = {8, c};   // bottom edge, east to west
        for (int rw = 7; rw >= 1; --rw) r[n++] = {rw, 0};  // west edge
        return r;
    }();
    return ring;
}

/// An outer grid vertex, in the 9x17 layout a chunk's vertices are stored in.
constexpr int outerIndex(int row, int col) { return row * 17 + col; }

TerrainLodIndices buildLodIndices(int level) {
    TerrainLodIndices out;
    if (level <= 0) return out;  // level 0 is the chunk's own, holes and all

    // 1, 2 or 4 grid steps between the vertices this level keeps: 81, 25 and 9
    // of them, which are the 9x9 outer grid at full, half and quarter density.
    const int step = 1 << (level - 1);
    const int cells = 8 / step;
    out.indices.reserve(static_cast<size_t>(cells) * cells * 6 + kChunkSkirtVertices * 6);

    for (int r = 0; r < cells; ++r) {
        for (int c = 0; c < cells; ++c) {
            const int r0 = r * step;
            const int c0 = c * step;
            const int r1 = r0 + step;
            const int c1 = c0 + step;
            const TerrainIndex tl = outerIndex(r0, c0);
            const TerrainIndex tr = outerIndex(r0, c1);
            const TerrainIndex bl = outerIndex(r1, c0);
            const TerrainIndex br = outerIndex(r1, c1);
            // The same winding the full-detail fan uses, so a reduced chunk is
            // not the one chunk in the scene facing the other way.
            out.indices.push_back(tl);
            out.indices.push_back(bl);
            out.indices.push_back(tr);
            out.indices.push_back(tr);
            out.indices.push_back(bl);
            out.indices.push_back(br);
        }
    }
    out.surfaceIndexCount = static_cast<uint32_t>(out.indices.size());

    // The skirt: a band from each ring vertex to the one hanging below it.
    // Every ring vertex, at every level - the ring is what the neighbour sees,
    // and a skirt that skipped the vertices this level dropped would leave the
    // gap it exists to close.
    const auto& ring = skirtRing();
    for (int k = 0; k < kChunkSkirtVertices; ++k) {
        const int next = (k + 1) % kChunkSkirtVertices;
        const TerrainIndex top = outerIndex(ring[k].first, ring[k].second);
        const TerrainIndex topNext = outerIndex(ring[next].first, ring[next].second);
        const TerrainIndex bottom = static_cast<TerrainIndex>(kChunkGridVertices + k);
        const TerrainIndex bottomNext = static_cast<TerrainIndex>(kChunkGridVertices + next);
        out.indices.push_back(top);
        out.indices.push_back(bottom);
        out.indices.push_back(topNext);
        out.indices.push_back(topNext);
        out.indices.push_back(bottom);
        out.indices.push_back(bottomNext);
    }
    return out;
}

}  // namespace

const TerrainLodIndices& terrainLodIndices(int level) {
    static const std::array<TerrainLodIndices, kTerrainLodLevels> sets = [] {
        std::array<TerrainLodIndices, kTerrainLodLevels> s{};
        for (int i = 0; i < kTerrainLodLevels; ++i) s[i] = buildLodIndices(i);
        return s;
    }();
    if (level < 0) level = 0;
    if (level >= kTerrainLodLevels) level = kTerrainLodLevels - 1;
    return sets[level];
}

int terrainLodForDistance(float distance, float viewDistance, int maxLevel) {
    if (maxLevel <= 0 || viewDistance <= 1.0f) return 0;
    const float t = distance / viewDistance;
    int level = 0;
    if (t > 0.6f) level = 3;
    else if (t > 0.3f) level = 2;
    else if (t > 0.12f) level = 1;
    return level < maxLevel ? level : maxLevel;
}

TerrainMesh TerrainMeshGenerator::generate(const ADTTerrain& terrain) {
    TerrainMesh mesh;

    if (!terrain.isLoaded()) {
        LOG_WARNING("Attempting to generate mesh from unloaded terrain");
        return mesh;
    }


    // Copy texture list
    mesh.textures = terrain.textures;

    // Generate mesh for each chunk
    int validCount = 0;
    bool loggedFirstChunk = false;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            const MapChunk& chunk = terrain.getChunk(x, y);

            if (chunk.hasHeightMap()) {
                mesh.getChunk(x, y) = generateChunkMesh(chunk, x, y, terrain.coord.x, terrain.coord.y);
                validCount++;

                // Debug: log first chunk world position
                if (!loggedFirstChunk) {
                    loggedFirstChunk = true;
                    LOG_DEBUG("First terrain chunk world pos: (", chunk.position[0], ", ",
                              chunk.position[1], ", ", chunk.position[2], ")");
                }
            }
        }
    }

    mesh.validChunkCount = validCount;


    return mesh;
}

ChunkMesh TerrainMeshGenerator::generateChunkMesh(const MapChunk& chunk, int chunkX, int chunkY, int tileX, int tileY) {
    ChunkMesh mesh;

    mesh.chunkX = chunkX;
    mesh.chunkY = chunkY;

    // Compute render-space XY from tile/chunk indices (MCNK position fields are unreliable).
    // tileX increases southward (renderY axis), tileY increases eastward (renderX axis).
    // NW corner of tile: renderX = (32-tileY)*TILE_SIZE, renderY = (32-tileX)*TILE_SIZE
    // Each chunk step goes east (–renderX) or south (–renderY).
    const float tileNW_renderX = (32.0f - static_cast<float>(tileY)) * core::coords::TILE_SIZE;
    const float tileNW_renderY = (32.0f - static_cast<float>(tileX)) * core::coords::TILE_SIZE;
    mesh.worldX = tileNW_renderX - static_cast<float>(chunkY) * CHUNK_SIZE;  // iy controls renderX (east-west)
    mesh.worldY = tileNW_renderY - static_cast<float>(chunkX) * CHUNK_SIZE;  // ix controls renderY (north-south)
    mesh.worldZ = chunk.position[2];  // height base (wowZ) from MCNK offset 112

    // Debug: log chunk positions for first tile
    static int posLogCount = 0;
    if (posLogCount < 5) {
        posLogCount++;
        LOG_INFO("Terrain chunk: tile(", tileX, ",", tileY, ") ix=", chunkX, " iy=", chunkY,
                 " worldXY=(", mesh.worldX, ",", mesh.worldY, ",", mesh.worldZ, ")",
                 " mcnk=(", chunk.position[0], ",", chunk.position[1], ",", chunk.position[2], ")");
    }

    // Generate vertices from heightmap (pass chunk grid indices and tile coords)
    mesh.vertices = generateVertices(chunk, chunkX, chunkY, tileX, tileY);

    // Generate triangle indices (checks for holes)
    mesh.indices = generateIndices(chunk);

    // Debug: verify mesh integrity (one-time)
    static bool debugLogged = false;
    if (!debugLogged && chunkX == 0 && chunkY == 0) {
        debugLogged = true;
        LOG_INFO("Terrain mesh debug: ", mesh.vertices.size(), " vertices, ",
                 mesh.indices.size(), " indices (", mesh.indices.size() / 3, " triangles)");

        // Verify all indices are in bounds
        int maxIndex = 0;
        int minIndex = 9999;
        for (auto idx : mesh.indices) {
            if (static_cast<int>(idx) > maxIndex) maxIndex = idx;
            if (static_cast<int>(idx) < minIndex) minIndex = idx;
        }
        LOG_INFO("Index range: [", minIndex, ", ", maxIndex, "] (expected [0, 144])");

        if (maxIndex >= static_cast<int>(mesh.vertices.size())) {
            LOG_ERROR("INDEX OUT OF BOUNDS! Max index ", maxIndex, " >= vertex count ", mesh.vertices.size());
        }

        // Check for invalid vertex positions
        int invalidCount = 0;
        for (const auto& v : mesh.vertices) {
            if (!std::isfinite(v.position[0]) || !std::isfinite(v.position[1]) || !std::isfinite(v.position[2])) {
                invalidCount++;
            }
        }
        if (invalidCount > 0) {
            LOG_ERROR("Found ", invalidCount, " vertices with invalid positions!");
        }
    }

    // Copy texture layers
    for (size_t layerIdx = 0; layerIdx < chunk.layers.size(); layerIdx++) {
        const auto& layer = chunk.layers[layerIdx];
        ChunkMesh::LayerInfo layerInfo;
        layerInfo.textureId = layer.textureId;
        layerInfo.flags = layer.flags;

        // The layer's alpha map, through the one decoder.
        //
        // This was a third copy of it - the header beside decodeLayerAlpha
        // says it exists because two had already grown, and this is the one
        // that was left. It is also the copy that reaches the screen: the
        // renderer builds its blend textures from these. So the fix that fills
        // a four-bit map's unpainted last row and column landed in the decoder
        // the grass and the queries use, and the ground went on drawing a
        // strip of something else along two sides of every chunk.
        //
        // Zero for what a layer's data does not reach, which is what this
        // builder has always used: an absent alpha means the layer covers
        // nothing, and the base layer beneath it shows through.
        //
        // And nothing at all when there is no map to decode. The renderer
        // reads an empty one as "this layer covers everything under it" and
        // hands it the opaque texture, so a map that failed to decode must
        // leave the vector alone rather than fill it with zeroes - which would
        // be the opposite instruction.
        if (layer.useAlpha()) {
            std::vector<uint8_t> decoded;
            if (decodeLayerAlpha(chunk, layerIdx, decoded, 0)) {
                layerInfo.alphaData = std::move(decoded);
            }
        }

        mesh.layers.push_back(layerInfo);
    }

    return mesh;
}

std::vector<TerrainVertex> TerrainMeshGenerator::generateVertices(const MapChunk& chunk, int chunkX, int chunkY, int tileX, int tileY) {
    std::vector<TerrainVertex> vertices;
    vertices.reserve(145);  // 145 vertices total

    const HeightMap& heightMap = chunk.heightMap;

    // WoW terrain uses 145 heights stored in a 9x17 row-major grid layout.
    //
    // Vertex XY is derived as a SINGLE multiply from the tile index:
    //   pos = TILE_SIZE * (32 - tile - gridStep/128)
    // where gridStep = chunk*8 + vertexOffset runs 0..128 across the 16 chunks of a
    // tile. The previous form subtracted the chunk base and the per-vertex step
    // separately, so a tile's far edge (…*C - C) and the neighbouring tile's near edge
    // ((…-1)*C) rounded to slightly different float32 values - a sub-yard gap that
    // opened hairline "blue" T-junction cracks between tiles, worst far from the map
    // origin (across Kalimdor). Collapsing both to one multiply makes the shared edge
    // bit-identical on either side, closing the seam without the overlap hacks.
    const float TS = core::coords::TILE_SIZE;
    constexpr float kStepsPerTile = 128.0f;  // 16 chunks * 8 vertex steps
    const float chunkBaseZ = chunk.position[2];  // height base (wowZ) from MCNK offset 112

    for (int index = 0; index < 145; index++) {
        int y = index / 17;  // Row (0-8)
        int x = index % 17;  // Column (0-16)

        // Columns 9-16 are offset by 0.5 units (wowee exact logic)
        float offsetX = static_cast<float>(x);
        float offsetY = static_cast<float>(y);

        if (x > 8) {
            offsetY += 0.5f;
            offsetX -= 8.5f;
        }

        TerrainVertex vertex;

        // Position in render space:
        //   MCVT rows (offsetY) go west→east = renderX decreasing
        //   MCVT columns (offsetX) go north→south = renderY decreasing
        // NaN heights are clamped - WHM load scrubs but mid-edit terrain
        // can briefly carry NaN before stitchEdges runs, and a single NaN
        // vertex would propagate into normal computations and crash culling.
        float h = heightMap.heights[index];
        if (!std::isfinite(h)) h = 0.0f;
        // Fractional grid position within the tile along each render axis (0..128).
        const float gridX = static_cast<float>(chunkY) * 8.0f + offsetY;  // row → renderX (west→east)
        const float gridY = static_cast<float>(chunkX) * 8.0f + offsetX;  // col → renderY (north→south)
        vertex.position[0] = (32.0f - static_cast<float>(tileY) - gridX / kStepsPerTile) * TS;  // renderX
        vertex.position[1] = (32.0f - static_cast<float>(tileX) - gridY / kStepsPerTile) * TS;  // renderY
        vertex.position[2] = chunkBaseZ + h;                                                     // renderZ

        // Normal
        if (index * 3 + 2 < static_cast<int>(chunk.normals.size())) {
            decompressNormal(&chunk.normals[index * 3], vertex.normal);
        } else {
            // Default up normal
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 0.0f;
            vertex.normal[2] = 1.0f;
        }

        // Texture coordinates: world-aligned so patterns don't reset per chunk.
        // Tile each texture 4× per chunk (one repeat every ~8 yards) so the
        // texture's own pattern noise breaks up the chunk grid rather than
        // syncing with it. At 1 repeat/chunk the per-chunk alpha differences
        // read as obvious 33-yard squares; at 4× the pattern is small enough
        // that the eye no longer locks onto the chunk boundary.
        constexpr float texScale = 4.0f / CHUNK_SIZE;
        vertex.texCoord[0] = -vertex.position[1] * texScale;
        vertex.texCoord[1] = -vertex.position[0] * texScale;

        // The tangent frame, from those two lines: u runs along world -Y and v
        // along world -X, whatever the height does. Derived here beside the
        // coordinates it is the frame of, so the two cannot drift apart.
        {
            const glm::vec4 tf = rendering::gridTangent(
                glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]),
                glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f));
            vertex.tangent[0] = tf.x;
            vertex.tangent[1] = tf.y;
            vertex.tangent[2] = tf.z;
            vertex.tangent[3] = tf.w;
        }

        // Layer UV for alpha map sampling (0-1 range per chunk).
        // Sample at texel centers of the 64x64 alpha map to avoid edge seams.
        constexpr float alphaTexels = 64.0f;
        constexpr float alphaStep = (alphaTexels - 1.0f) / 8.0f; // 63 texels across 8 quads
        vertex.layerUV[0] = (offsetX * alphaStep + 0.5f) / alphaTexels;
        vertex.layerUV[1] = (offsetY * alphaStep + 0.5f) / alphaTexels;

        vertices.push_back(vertex);
    }

    // The skirt: one vertex hanging below each of the 32 outer-ring vertices.
    //
    // Appended to every chunk whether or not anything indexes it, because the
    // level a chunk draws at is decided per frame at the camera's distance and
    // the vertex buffer is uploaded once. Thirty-two vertices on a hundred and
    // forty-five is a fifth more terrain memory; regenerating a chunk's mesh
    // when it changed level would be a stall in the middle of walking.
    //
    // How far down: the chunk's own height range plus two yards. The error a
    // reduced level can introduce is bounded by how much the ground moves
    // inside the chunk, so a skirt that long always reaches past whatever the
    // neighbour drew, and one much longer than that is a wall of stretched
    // texture visible at the horizon.
    float minZ = vertices.empty() ? 0.0f : vertices[0].position[2];
    float maxZ = minZ;
    for (const auto& v : vertices) {
        minZ = std::min(minZ, v.position[2]);
        maxZ = std::max(maxZ, v.position[2]);
    }
    const float skirtDrop = (maxZ - minZ) + 2.0f;
    for (const auto& rc : skirtRing()) {
        TerrainVertex skirt = vertices[static_cast<size_t>(outerIndex(rc.first, rc.second))];
        skirt.position[2] -= skirtDrop;
        vertices.push_back(skirt);
    }

    return vertices;
}

std::vector<TerrainIndex> TerrainMeshGenerator::generateIndices(const MapChunk& chunk) {
    std::vector<TerrainIndex> indices;
    indices.reserve(768);  // 8x8 quads * 4 triangles * 3 indices = 768

    // Generate indices based on 9x17 grid layout (matching wowee.js)
    // Each quad uses a center vertex with 4 surrounding vertices
    // Index offsets from center: -9, -8, +9, +8

    int holesSkipped = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            // Skip quads that are marked as holes (cave entrances, etc.)
            if (chunk.isHole(y, x)) {
                holesSkipped++;
                continue;
            }

            // Center vertex index in the 9x17 grid
            int center = 9 + y * 17 + x;

            // Four triangles per quad
            // Using CCW winding when viewed from +Z (top-down)
            int tl = center - 9;  // top-left outer
            int tr = center - 8;  // top-right outer
            int bl = center + 8;  // bottom-left outer
            int br = center + 9;  // bottom-right outer

            // Triangle 1: top (center, tl, tr)
            indices.push_back(center);
            indices.push_back(tl);
            indices.push_back(tr);

            // Triangle 2: right (center, tr, br)
            indices.push_back(center);
            indices.push_back(tr);
            indices.push_back(br);

            // Triangle 3: bottom (center, br, bl)
            indices.push_back(center);
            indices.push_back(br);
            indices.push_back(bl);

            // Triangle 4: left (center, bl, tl)
            indices.push_back(center);
            indices.push_back(bl);
            indices.push_back(tl);
        }
    }

    // Debug: log if any holes were skipped (one-time per session)
    static bool holesLogged = false;
    if (!holesLogged && holesSkipped > 0) {
        holesLogged = true;
        LOG_INFO("Terrain holes: skipped ", holesSkipped, " quads due to hole mask (holes=0x",
                 std::hex, chunk.holes, std::dec, ")");
    }

    return indices;
}
void TerrainMeshGenerator::decompressNormal(const int8_t* compressedNormal, float* normal) {
    // WoW stores normals as signed bytes (-127 to 127)
    // Convert to float and normalize

    float x = compressedNormal[0] / 127.0f;
    float y = compressedNormal[1] / 127.0f;
    float z = compressedNormal[2] / 127.0f;

    // Normalize
    float length = std::sqrt(x * x + y * y + z * z);
    if (length > 0.0001f) {
        normal[0] = x / length;
        normal[1] = y / length;
        normal[2] = z / length;
    } else {
        // Default up normal if degenerate
        normal[0] = 0.0f;
        normal[1] = 0.0f;
        normal[2] = 1.0f;
    }
}

bool TerrainMeshGenerator::chunkFractionsAt(const float chunkPosition[3],
                                            float glX, float glY, float unitSize,
                                            float& fracX, float& fracY) {
    const float maxX = chunkPosition[0];
    const float minX = chunkPosition[0] - 8.0f * unitSize;
    const float maxY = chunkPosition[1];
    const float minY = chunkPosition[1] - 8.0f * unitSize;
    if (glX < minX || glX > maxX || glY < minY || glY > maxY) return false;

    fracY = std::clamp((chunkPosition[0] - glX) / unitSize, 0.0f, 8.0f);
    fracX = std::clamp((chunkPosition[1] - glY) / unitSize, 0.0f, 8.0f);
    return true;
}

glm::vec3 TerrainMeshGenerator::chunkSurfacePoint(const float chunkPosition[3],
                                                  const HeightMap& heightMap,
                                                  float fracX, float fracY,
                                                  float unitSize) {
    // The axes cross: world X runs against grid Y and world Y against grid X.
    const float worldX = chunkPosition[0] - fracY * unitSize;
    const float worldY = chunkPosition[1] - fracX * unitSize;

    // The surface is four triangles fanned from the quad's centre vertex, which
    // is what generateChunkMesh emits and therefore what the player sees.
    //
    // Interpolating the four outer corners bilinearly instead ignores that
    // centre vertex completely, and MCVT puts it wherever the terrain artist
    // needed it - on a hillside it commonly sits a yard or two off the plane of
    // its corners. The floor query then answered lower than the ground being
    // drawn and the player sank through a slope that looked gentle.
    const int qx = glm::clamp(static_cast<int>(std::floor(fracX)), 0, 7);
    const int qy = glm::clamp(static_cast<int>(std::floor(fracY)), 0, 7);
    const float u = glm::clamp(fracX - static_cast<float>(qx), 0.0f, 1.0f);
    const float v = glm::clamp(fracY - static_cast<float>(qy), 0.0f, 1.0f);

    const float hTL = heightMap.getHeight(qx,     qy);
    const float hTR = heightMap.getHeight(qx + 1, qy);
    const float hBL = heightMap.getHeight(qx,     qy + 1);
    const float hBR = heightMap.getHeight(qx + 1, qy + 1);
    // Centre vertex of this quad in the interleaved 9x17 grid, at (0.5, 0.5).
    const int centreIndex = 9 + qy * 17 + qx;
    const float hC = (centreIndex >= 0 && centreIndex < static_cast<int>(heightMap.heights.size()))
                     ? heightMap.heights[centreIndex]
                     : 0.25f * (hTL + hTR + hBL + hBR);

    // Which of the four wedges the point is in, then barycentric within it.
    // Each wedge is the centre plus one edge of the quad, so two of the three
    // weights are the corner heights and the third is always the centre.
    float hA, hB;      // the two corners of the wedge
    float wA, wB;      // their weights
    if (u > v) {
        if (u + v < 1.0f) {          // top wedge: TL, TR
            hA = hTL; hB = hTR; wA = 1.0f - u - v; wB = u - v;
        } else {                      // right wedge: TR, BR
            hA = hTR; hB = hBR; wA = u - v;        wB = u + v - 1.0f;
        }
    } else {
        if (u + v < 1.0f) {          // left wedge: TL, BL
            hA = hTL; hB = hBL; wA = 1.0f - u - v; wB = v - u;
        } else {                      // bottom wedge: BL, BR
            hA = hBL; hB = hBR; wA = v - u;        wB = u + v - 1.0f;
        }
    }
    const float wC = 1.0f - wA - wB;
    const float worldZ = chunkPosition[2] + (hA * wA + hB * wB + hC * wC);
    return glm::vec3(worldX, worldY, worldZ);
}
} // namespace pipeline
} // namespace wowee
