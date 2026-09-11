#pragma once

/**
 * tangent_frame.hpp - the tangent basis a normal map is read in.
 *
 * A normal map stores its normals in the surface's own frame: X along the
 * texture's u axis, Y along v, Z out of the surface. To use one, a mesh has to
 * carry that frame per vertex, and the frame has to be derived from the same
 * positions and the same texture coordinates the map was authored against.
 * Lengyel's method is the standard derivation: solve the two triangle edges
 * against the two texture-coordinate deltas for the tangent and bitangent,
 * accumulate per vertex, then orthogonalize against the vertex normal and keep
 * the handedness as a fourth component.
 *
 * This lived inside CharacterRenderer::setupModelBuffers, where it needed a
 * Vulkan context and a loaded M2 to reach and so had never been tested. It is
 * here because the M2 doodads and the terrain need the same frame and none of
 * the three should derive it differently - a normal map read in a frame that
 * disagrees with the one it was baked in lights the surface from the wrong
 * side, which reads as "the normal map is wrong" rather than as a basis fault.
 *
 * Pure: positions, texture coordinates, normals and indices in, one vec4 per
 * vertex out. No renderer, no device, no model format.
 *
 * RESERVED(phase-13, L7-pbr): the tangent attribute this fills is what GGX
 * reads anisotropy and a roughness sidecar through. The w component's sign
 * convention is settled here so that does not have to move it.
 */

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace wowee {
namespace rendering {

/// The frame for one mesh, as `vec4(tangent.xyz, handedness)`.
///
/// `handedness` is +1 or -1 and says which way the bitangent runs, so a shader
/// reconstructs it as `cross(normal, tangent.xyz) * tangent.w` rather than
/// carrying a second vector. Mirrored UV shells - which every character model
/// has, because half of one is the other half flipped - are exactly the case
/// where the sign matters and where getting it wrong lights one side of a face
/// from the wrong direction.
///
/// A vertex no triangle reaches, or one whose triangles are all degenerate in
/// texture space, gets `vec4(1, 0, 0, 1)`: an arbitrary frame, which is what
/// the surface deserves when its texture coordinates do not describe one. It is
/// never zero, because a zero tangent normalizes to NaN in a shader and a NaN
/// normal is a black or white pixel rather than a subtly wrong one.
struct TangentFrames {
    std::vector<glm::vec4> tangents;
};

/// Accumulate and orthogonalize, over a triangle list.
///
/// `indices` is three per triangle. An index past the end of the vertex arrays
/// skips its whole triangle rather than reading out of bounds: a broken model
/// should lose a triangle's contribution, not the process.
///
/// `texCoords` must be the set the normal map is addressed by - for an M2 that
/// is the first, not the second, which is the environment-map coordinate.
template <typename IndexT>
TangentFrames computeTangentFrames(const std::vector<glm::vec3>& positions,
                                   const std::vector<glm::vec2>& texCoords,
                                   const std::vector<glm::vec3>& normals,
                                   const std::vector<IndexT>& indices) {
    TangentFrames out;
    const size_t vertexCount = positions.size();
    out.tangents.assign(vertexCount, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    if (vertexCount == 0 || texCoords.size() < vertexCount || normals.size() < vertexCount) {
        return out;
    }

    std::vector<glm::vec3> tanAccum(vertexCount, glm::vec3(0.0f));
    std::vector<glm::vec3> bitanAccum(vertexCount, glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const size_t i0 = static_cast<size_t>(indices[i]);
        const size_t i1 = static_cast<size_t>(indices[i + 1]);
        const size_t i2 = static_cast<size_t>(indices[i + 2]);
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;

        const glm::vec3 edge1 = positions[i1] - positions[i0];
        const glm::vec3 edge2 = positions[i2] - positions[i0];
        const glm::vec2 duv1 = texCoords[i1] - texCoords[i0];
        const glm::vec2 duv2 = texCoords[i2] - texCoords[i0];

        // The determinant of the texture-space edge matrix. Zero means the
        // triangle occupies no area in the texture - a seam collapsed to a
        // line, or a face with all three corners at one UV - and there is no
        // frame to be had from it.
        const float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (std::abs(det) < 1e-8f) continue;
        const float invDet = 1.0f / det;

        const glm::vec3 t = (edge1 * duv2.y - edge2 * duv1.y) * invDet;
        const glm::vec3 b = (edge2 * duv1.x - edge1 * duv2.x) * invDet;

        tanAccum[i0] += t;
        tanAccum[i1] += t;
        tanAccum[i2] += t;
        bitanAccum[i0] += b;
        bitanAccum[i1] += b;
        bitanAccum[i2] += b;
    }

    for (size_t i = 0; i < vertexCount; ++i) {
        const glm::vec3& n = normals[i];
        const glm::vec3& t = tanAccum[i];
        if (glm::dot(t, t) < 1e-8f) continue;  // keeps the default frame
        // Gram-Schmidt: the part of the accumulated tangent that lies in the
        // surface. The accumulation is over triangles whose normals differ from
        // the smoothed vertex normal, so the sum is generally not in the plane.
        const glm::vec3 rejected = t - n * glm::dot(n, t);
        if (glm::dot(rejected, rejected) < 1e-12f) continue;  // tangent is the normal
        const glm::vec3 tOrtho = glm::normalize(rejected);
        const float w = (glm::dot(glm::cross(n, t), bitanAccum[i]) < 0.0f) ? -1.0f : 1.0f;
        out.tangents[i] = glm::vec4(tOrtho, w);
    }
    return out;
}

/// The frame of a heightfield sampled on an axis-aligned grid, which terrain is.
///
/// No accumulation and no solve: a terrain vertex's texture coordinates are a
/// fixed scale of its world position, so the tangent is the world axis the u
/// coordinate runs along, projected into the surface, and the handedness is
/// whether the bitangent that falls out of it runs with the v axis or against
/// it. For this client's terrain the two axes are world -Y and world -X,
/// because terrain_mesh derives the coordinates as
/// `(-position.y, -position.x) * scale`.
///
/// That pair gives -1, not +1: with the tangent along -Y and the normal up,
/// `cross(normal, tangent)` points along +X while v runs along -X. A frame
/// stated the other way round reads the map's green channel upside down, which
/// lights every slope from the wrong vertical direction - the classic way for a
/// normal map to look "slightly wrong" rather than obviously broken, which is
/// why the handedness is derived here from the two axes rather than assumed.
inline glm::vec4 gridTangent(const glm::vec3& normal, const glm::vec3& uAxis,
                             const glm::vec3& vAxis) {
    const glm::vec3 rejected = uAxis - normal * glm::dot(normal, uAxis);
    const float len2 = glm::dot(rejected, rejected);
    if (len2 < 1e-12f) {
        // The surface is a wall facing along u. Nothing on this client's
        // terrain is - the heightmap is a function of x and y - but a vertical
        // skirt vertex copied from an edge could be, and NaN is not an answer.
        return glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    }
    const glm::vec3 t = rejected / std::sqrt(len2);
    const float w = (glm::dot(glm::cross(normal, t), vAxis) < 0.0f) ? -1.0f : 1.0f;
    return glm::vec4(t, w);
}

}  // namespace rendering
}  // namespace wowee
