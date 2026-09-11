#version 450

layout(set = 0, binding = 0) uniform PerFrame {
    mat4 view;
    mat4 projection;
    mat4 lightSpaceMatrix;
    vec4 lightDir;
    vec4 lightColor;
    vec4 ambientColor;
    vec4 viewPos;
    vec4 fogColor;
    vec4 fogParams;
    vec4 shadowParams;
};

layout(push_constant) uniform Push {
    mat4 model;
} push;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec2 aLayerUV;
// The tangent frame the generated normal maps are read in, derived
// analytically from the chunk grid - see gridTangent() in
// include/rendering/tangent_frame.hpp. w is the handedness.
//
// RESERVED(phase-13, L7-pbr): this attribute and the sidecar slot beside the
// layer normal maps are what GGX reads anisotropy and roughness through.
layout(location = 4) in vec4 aTangent;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec2 TexCoord;
layout(location = 3) out vec2 LayerUV;
layout(location = 4) out vec3 Tangent;
layout(location = 5) out vec3 Bitangent;

void main() {
    vec4 worldPos = push.model * vec4(aPosition, 1.0);
    FragPos = worldPos.xyz;
    Normal = aNormal;
    TexCoord = aTexCoord;
    LayerUV = aLayerUV;
    // Handed on unrotated, exactly as Normal above is: the terrain's model
    // matrix is a translation. Rotating one of the three and not the others
    // would be worse than rotating none.
    Tangent = aTangent.xyz;
    Bitangent = cross(aNormal, aTangent.xyz) * aTangent.w;
    gl_Position = projection * view * worldPos;
}
