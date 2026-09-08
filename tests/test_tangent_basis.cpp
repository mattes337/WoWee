#include <catch_amalgamated.hpp>

#include <cmath>

#include "rendering/tangent_basis.hpp"

using wowee::rendering::makeFiniteTangent;

namespace {

bool finite(const glm::vec4& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z) && std::isfinite(value.w);
}

}  // namespace

TEST_CASE("a triangle tangent parallel to its model normal gets a finite fallback",
          "[tangent-basis]") {
    // This is the same tangent calculation as setupModelBuffers. All source
    // values are finite, but this model-supplied normal is parallel to the
    // tangent produced by the geometry and UVs.
    const glm::vec3 edge1(0.0f, 0.0f, 2.0f);
    const glm::vec3 edge2(0.0f, 1.0f, 0.0f);
    const glm::vec2 duv1(1.0f, 0.0f);
    const glm::vec2 duv2(0.0f, 1.0f);
    const float inverseDeterminant = 1.0f /
        (duv1.x * duv2.y - duv2.x * duv1.y);
    const glm::vec3 tangent =
        (edge1 * duv2.y - edge2 * duv1.y) * inverseDeterminant;
    const glm::vec3 normal(0.0f, 0.0f, 1.0f);

    const glm::vec4 result = makeFiniteTangent(
        normal, tangent, glm::vec3(0.0f, 1.0f, 0.0f));

    CHECK(finite(result));
    CHECK(glm::length(glm::vec3(result)) == Catch::Approx(1.0f));
    CHECK(glm::dot(normal, glm::vec3(result)) == Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("nonunit normals still produce an orthonormal tangent",
          "[tangent-basis]") {
    const glm::vec3 normal(0.0f, 0.0f, 2.0f);
    const glm::vec4 result = makeFiniteTangent(
        normal, glm::vec3(1.0f, 0.0f, 2.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    CHECK(finite(result));
    CHECK(glm::length(glm::vec3(result)) == Catch::Approx(1.0f));
    CHECK(glm::dot(glm::normalize(normal), glm::vec3(result)) ==
          Catch::Approx(0.0f).margin(1e-6f));
}

TEST_CASE("degenerate finite normals and overflowed lengths use a finite basis",
          "[tangent-basis]") {
    const glm::vec4 zeroNormal = makeFiniteTangent(
        glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f));
    const glm::vec4 hugeNormal = makeFiniteTangent(
        glm::vec3(1e20f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));

    CHECK(finite(zeroNormal));
    CHECK(finite(hugeNormal));
    CHECK(glm::length(glm::vec3(zeroNormal)) == Catch::Approx(1.0f));
    CHECK(glm::length(glm::vec3(hugeNormal)) == Catch::Approx(1.0f));
}

TEST_CASE("a regular tangent keeps its handedness", "[tangent-basis]") {
    const glm::vec4 positive = makeFiniteTangent(
        glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(2.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec4 negative = makeFiniteTangent(
        glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(2.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, -1.0f, 0.0f));

    CHECK(positive == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    CHECK(negative == glm::vec4(1.0f, 0.0f, 0.0f, -1.0f));
}
