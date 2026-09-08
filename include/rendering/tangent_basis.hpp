#pragma once

#include <cmath>

#include <glm/glm.hpp>

namespace wowee::rendering {

inline bool isFinite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

/// Orthogonalize an accumulated tangent against a model normal without
/// allowing degenerate model data to put NaNs in the vertex buffer.
inline glm::vec4 makeFiniteTangent(glm::vec3 normal,
                                   const glm::vec3& tangent,
                                   const glm::vec3& bitangent) {
    constexpr float epsilon = 1e-8f;

    const float normalLengthSquared = glm::dot(normal, normal);
    if (!isFinite(normal) || !std::isfinite(normalLengthSquared) ||
        normalLengthSquared < epsilon) {
        normal = glm::vec3(0.0f, 0.0f, 1.0f);
    } else {
        normal *= 1.0f / std::sqrt(normalLengthSquared);
    }

    glm::vec3 orthogonal = tangent - normal * glm::dot(normal, tangent);
    const float orthogonalLengthSquared = glm::dot(orthogonal, orthogonal);
    if (!isFinite(orthogonal) || !std::isfinite(orthogonalLengthSquared) ||
        orthogonalLengthSquared < epsilon) {
        const glm::vec3 reference = std::abs(normal.x) < 0.9f
            ? glm::vec3(1.0f, 0.0f, 0.0f)
            : glm::vec3(0.0f, 1.0f, 0.0f);
        orthogonal = glm::normalize(glm::cross(reference, normal));
    } else {
        orthogonal *= 1.0f / std::sqrt(orthogonalLengthSquared);
    }

    float handedness = 1.0f;
    if (isFinite(bitangent) &&
        glm::dot(glm::cross(normal, orthogonal), bitangent) < 0.0f) {
        handedness = -1.0f;
    }
    return glm::vec4(orthogonal, handedness);
}

}  // namespace wowee::rendering
