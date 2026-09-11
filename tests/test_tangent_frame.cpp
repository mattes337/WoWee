// The tangent basis a normal map is read in.
//
// This code shipped for a year inside CharacterRenderer::setupModelBuffers,
// where reaching it needed a Vulkan device and a loaded M2, so nothing had ever
// checked it. What it gets wrong is not loud: a handedness flipped on a
// mirrored UV shell lights one side of a face from the wrong direction, which
// reads as a bad normal map rather than as a bad basis, and a degenerate
// triangle that reaches the normalize puts a NaN in a vertex attribute, which
// is a black or white pixel with no other symptom.
//
// So: a flat quad whose tangent is knowable by hand, a mirrored one whose
// handedness must flip, and every degenerate input that used to divide by
// something near zero.

#include "rendering/tangent_frame.hpp"

#include <catch_amalgamated.hpp>

#include <cmath>
#include <vector>

using wowee::rendering::computeTangentFrames;
using wowee::rendering::gridTangent;

namespace {

bool nearly(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
           std::abs(a.z - b.z) < eps;
}

bool finite(const glm::vec4& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::isfinite(v.w);
}

}  // namespace

TEST_CASE("a flat quad's tangent runs along its u axis", "[tangent]") {
    // A unit quad in the XY plane, u along +X and v along +Y. The tangent is
    // then +X exactly, and the handedness is positive.
    const std::vector<glm::vec3> pos = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<glm::vec2> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const std::vector<glm::vec3> nrm = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};

    const auto frames = computeTangentFrames(pos, uv, nrm, idx);
    REQUIRE(frames.tangents.size() == 4);
    for (const auto& t : frames.tangents) {
        INFO("tangent " << t.x << "," << t.y << "," << t.z << " w=" << t.w);
        CHECK(finite(t));
        CHECK(nearly(glm::vec3(t), glm::vec3(1, 0, 0)));
        CHECK(t.w == Catch::Approx(1.0f));
    }
}

TEST_CASE("the tangent follows the texture, not the geometry", "[tangent]") {
    // The same quad with u running along +Y instead. The positions have not
    // moved; the frame must, because a normal map baked against these
    // coordinates has its X axis pointing that way.
    const std::vector<glm::vec3> pos = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<glm::vec2> uv = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
    const std::vector<glm::vec3> nrm = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};

    const auto frames = computeTangentFrames(pos, uv, nrm, idx);
    for (const auto& t : frames.tangents) {
        INFO("tangent " << t.x << "," << t.y << "," << t.z);
        CHECK(nearly(glm::vec3(t), glm::vec3(0, 1, 0)));
    }
}

TEST_CASE("a mirrored UV shell flips the handedness", "[tangent]") {
    // Half of every character model is the other half with its u coordinate
    // reversed. The bitangent then runs the other way round the normal, and
    // that is the whole reason the frame carries a fourth component instead of
    // being three floats.
    const std::vector<glm::vec3> pos = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    const std::vector<glm::vec2> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const std::vector<glm::vec2> mirrored = {{1, 0}, {0, 0}, {0, 1}, {1, 1}};
    const std::vector<glm::vec3> nrm = {
        {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1}};
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};

    const auto normal = computeTangentFrames(pos, uv, nrm, idx);
    const auto flipped = computeTangentFrames(pos, mirrored, nrm, idx);
    for (size_t i = 0; i < 4; ++i) {
        INFO("vertex " << i);
        CHECK(normal.tangents[i].w == Catch::Approx(1.0f));
        CHECK(flipped.tangents[i].w == Catch::Approx(-1.0f));
        // And the tangent itself points the other way, which is what makes the
        // reconstructed bitangent land back where the geometry wants it.
        CHECK(nearly(glm::vec3(flipped.tangents[i]), -glm::vec3(normal.tangents[i])));
    }
}

TEST_CASE("the tangent is orthogonal to the vertex normal", "[tangent]") {
    // A bent quad: the shared edge's vertices carry a smoothed normal that no
    // triangle's own plane matches, so the accumulated tangent is out of the
    // surface until it is orthogonalized. A shader that builds its basis from
    // this assumes it already has been.
    const std::vector<glm::vec3> pos = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0.5f}, {0, 1, 0.5f}};
    const std::vector<glm::vec2> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    std::vector<glm::vec3> nrm = {
        {0, -0.3f, 1}, {0, -0.3f, 1}, {0, -0.7f, 1}, {0, -0.7f, 1}};
    for (auto& n : nrm) n = glm::normalize(n);
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};

    const auto frames = computeTangentFrames(pos, uv, nrm, idx);
    for (size_t i = 0; i < 4; ++i) {
        const glm::vec3 t(frames.tangents[i]);
        INFO("vertex " << i);
        CHECK(std::abs(glm::length(t) - 1.0f) < 1e-4f);
        CHECK(std::abs(glm::dot(t, nrm[i])) < 1e-4f);
    }
}

TEST_CASE("degenerate input never produces a NaN", "[tangent]") {
    const std::vector<glm::vec3> nrm(4, glm::vec3(0, 0, 1));
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3};

    SECTION("all three corners at one texture coordinate") {
        const std::vector<glm::vec3> pos = {
            {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const std::vector<glm::vec2> uv(4, glm::vec2(0.25f, 0.75f));
        const auto frames = computeTangentFrames(pos, uv, nrm, idx);
        for (const auto& t : frames.tangents) {
            CHECK(finite(t));
            CHECK(glm::length(glm::vec3(t)) == Catch::Approx(1.0f));
        }
    }

    SECTION("a UV seam collapsed to a line") {
        const std::vector<glm::vec3> pos = {
            {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const std::vector<glm::vec2> uv = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
        const auto frames = computeTangentFrames(pos, uv, nrm, idx);
        for (const auto& t : frames.tangents) CHECK(finite(t));
    }

    SECTION("indices past the end of the vertex list") {
        const std::vector<glm::vec3> pos = {
            {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const std::vector<glm::vec2> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const std::vector<uint16_t> bad = {0, 1, 99, 0, 2, 3};
        const auto frames = computeTangentFrames(pos, uv, nrm, bad);
        REQUIRE(frames.tangents.size() == 4);
        for (const auto& t : frames.tangents) CHECK(finite(t));
    }

    SECTION("no vertices at all") {
        const auto frames = computeTangentFrames(std::vector<glm::vec3>{},
                                                 std::vector<glm::vec2>{},
                                                 std::vector<glm::vec3>{},
                                                 std::vector<uint16_t>{});
        CHECK(frames.tangents.empty());
    }

    SECTION("an index list that is not a whole number of triangles") {
        const std::vector<glm::vec3> pos = {
            {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        const std::vector<glm::vec2> uv = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const std::vector<uint16_t> ragged = {0, 1, 2, 0, 2};
        const auto frames = computeTangentFrames(pos, uv, nrm, ragged);
        for (const auto& t : frames.tangents) CHECK(finite(t));
    }
}

TEST_CASE("a grid tangent is the u axis laid into the surface", "[tangent]") {
    // Flat ground: the tangent is the u axis unchanged.
    const glm::vec4 flat = gridTangent(glm::vec3(0, 0, 1), glm::vec3(0, -1, 0),
                                       glm::vec3(-1, 0, 0));
    CHECK(nearly(glm::vec3(flat), glm::vec3(0, -1, 0)));
    // cross(up, -Y) is +X and v runs along -X, so the bitangent runs against
    // the cross product: the handedness this grid actually has is -1.
    CHECK(flat.w == Catch::Approx(-1.0f));

    // A slope: the tangent tilts with it and stays perpendicular to the normal.
    const glm::vec3 slope = glm::normalize(glm::vec3(0.0f, 0.4f, 1.0f));
    const glm::vec4 tilted = gridTangent(slope, glm::vec3(0, -1, 0),
                                         glm::vec3(-1, 0, 0));
    CHECK(std::abs(glm::dot(glm::vec3(tilted), slope)) < 1e-5f);
    CHECK(std::abs(glm::length(glm::vec3(tilted)) - 1.0f) < 1e-5f);

    // A wall facing straight along u, which nothing on this terrain is but a
    // skirt vertex could look like. The answer is arbitrary and finite.
    const glm::vec4 wall = gridTangent(glm::vec3(0, -1, 0), glm::vec3(0, -1, 0),
                                       glm::vec3(-1, 0, 0));
    CHECK(finite(wall));
    CHECK(glm::length(glm::vec3(wall)) == Catch::Approx(1.0f));
}
