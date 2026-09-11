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
    vec4 playerPos;   // xyz = player world position, w = horizontal speed
    vec4 playerWake;  // xyz = trailing player position (springback reference)
    vec4 localLightPosRadius[64];
    vec4 localLightColorIntensity[64];
    ivec4 localLightMeta;
    // ---- appended in phase 01, all at once. See vk_frame_data.hpp. ----
    // The cascade set. cascadeMatrix[0] mirrors lightSpaceMatrix, so the two
    // shadow paths agree about the nearest cascade.
    mat4 cascadeMatrix[4];
    vec4 shadowSplits;   // distance from the camera each cascade ends at
    ivec4 shadowMeta;    // x = count, y = blend band (yd), z = filter
    // RESERVED(phase-15, A2-volumetric-fog): fogHeight.w and fogSunColor are
    // the froxel volume's inputs too; declared with the fog parameters so this
    // block moves once.
    vec4 fogHeight;    // x = base height, y = density/yd, z = 1/scale height, w = aerial
    vec4 fogSunColor;  // rgb = sun in-scatter colour, w unused
    // RESERVED(phase-11, L5-sky-probes): SH9 ambient. Zero-filled; unread.
    vec4 skySH[7];
};

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(set = 1, binding = 1) uniform CharMaterial {
    float opacity;
    int alphaTest;
    int colorKeyBlack;
    int unlit;
    float emissiveBoost;
    // Keep these as scalar floats to match the C++ UBO packing. A std140 vec3
    // would insert padding here and shift the following material flags.
    float emissiveTintR;
    float emissiveTintG;
    float emissiveTintB;
    float specularIntensity;
    int enableNormalMap;
    int enablePOM;
    float pomScale;
    int pomMaxSamples;
    float heightMapVariance;
    float normalMapStrength;
    int hairMaterial;
    // How a two-layer M2 material combines its layers, from the texture unit's
    // shader id. 0 means one layer and nothing to combine.
    //
    // The names are Blizzard's, read <layer0 op>_<layer1 op>. "Opaque" on the
    // first means its alpha is not used; "NA" on the second means the second's
    // alpha is not used. Getting this wrong is not subtle on the login screen:
    // its light shafts and aurora carry their falloff in layer 1, and taking
    // alpha from the wrong layer leaves their quad edges showing.
    //   1 Opaque_Mod   2 Opaque_Mod2x  3 Opaque_Mod2xNA  4 Opaque_Opaque
    //   5 Mod_Mod      6 Mod_Mod2x     7 Mod_Add         8 Mod_Mod2xNA
    //   9 Mod_AddNA   10 Mod_Opaque
    int texCombiner;
};

layout(set = 1, binding = 2) uniform sampler2D uNormalHeightMap;
// The material's second texture layer; a white 1x1 when it has none, so
// modulating by it is the identity and the branch below stays cheap.
layout(set = 1, binding = 3) uniform sampler2D uTexture2;

layout(set = 0, binding = 1) uniform sampler2DShadow uShadowMap;
// The cascaded pair. Declared beside the single map rather than
// replacing it, so a pipeline specialized for one cascade compiles the same
// module it always did - see the header of shadow_common.glsl. The array view
// is over the same image; the second is the same image again through a plain
// sampler, which is the only way a PCSS blocker search can read a depth
// instead of comparing against it.
layout(set = 0, binding = 2) uniform sampler2DArrayShadow uShadowMapArray;
layout(set = 0, binding = 3) uniform sampler2DArray uShadowMapDepth;

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec2 TexCoord;
layout(location = 3) in vec3 Tangent;
layout(location = 4) in vec3 Bitangent;
layout(location = 5) in vec2 TexCoord2;

layout(location = 0) out vec4 outColor;

const int PREVIEW_SIMPLE_TEXTURE_MODE = -31336;

#define WOWEE_HAS_NORMAL_HEIGHT_MAP
#include "lit_common.glsl"

vec3 localLightContribution(vec3 pos, vec3 normal, vec3 albedo) {
    vec3 sum = vec3(0.0);
    for (int i = 0; i < min(localLightMeta.x, 64); ++i) {
        vec3 toLight = localLightPosRadius[i].xyz - pos;
        float radius = localLightPosRadius[i].w;
        // Rejected on the squared distance, before the square root and the
        // divide: most of the sixty-four are out of range of any one pixel,
        // and this is what each of them costs.
        float distSq = dot(toLight, toLight);
        if (radius <= 0.0 || distSq >= radius * radius) continue;
        float dist = sqrt(distSq);
        float attenuation = 1.0 - dist / radius;
        attenuation *= attenuation;
        float wrappedDiffuse = 0.22 + 0.78 * max(dot(normal, toLight / max(dist, 0.001)), 0.0);
        sum += albedo * localLightColorIntensity[i].rgb *
               (localLightColorIntensity[i].w * attenuation * wrappedDiffuse);
    }
    return sum;
}

vec3 safeNormalize(vec3 v, vec3 fallback) {
    float len2 = dot(v, v);
    if (len2 > 1e-8) {
        return v * inversesqrt(len2);
    }
    return fallback;
}

vec3 fallbackTangent(vec3 n) {
    vec3 axis = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
    return safeNormalize(cross(axis, n), vec3(1.0, 0.0, 0.0));
}

bool finiteVec3(vec3 v) {
    return all(equal(v, v)) && all(lessThan(abs(v), vec3(1e10)));
}

bool isMagentaKeyColor(vec4 color) {
    return color.r >= 0.58 && color.b >= 0.58 && color.g <= 0.48 &&
           color.r >= color.g + 0.22 && color.b >= color.g + 0.22 &&
           abs(color.r - color.b) <= 0.38;
}

ivec2 wrapPreviewTexel(ivec2 texel, ivec2 texSize) {
    return ivec2((texel.x % texSize.x + texSize.x) % texSize.x,
                 (texel.y % texSize.y + texSize.y) % texSize.y);
}

vec4 combineLayers(vec4 t0, vec4 t1, int mode) {
    if (mode == 1)  return vec4(t0.rgb * t1.rgb,       t1.a);
    if (mode == 2)  return vec4(t0.rgb * t1.rgb * 2.0, t1.a);
    if (mode == 3)  return vec4(t0.rgb * t1.rgb * 2.0, 1.0);
    if (mode == 4)  return vec4(t0.rgb * t1.rgb,       1.0);
    if (mode == 5)  return vec4(t0.rgb * t1.rgb,       t0.a * t1.a);
    if (mode == 6)  return vec4(t0.rgb * t1.rgb * 2.0, t0.a * t1.a);
    if (mode == 7)  return vec4(t0.rgb + t1.rgb,       t0.a + t1.a);
    if (mode == 8)  return vec4(t0.rgb * t1.rgb * 2.0, t0.a);
    if (mode == 9)  return vec4(t0.rgb + t1.rgb,       t0.a);
    if (mode == 10) return vec4(t0.rgb * t1.rgb,       t0.a);
    return t0;
}

vec4 samplePreviewTexture(sampler2D tex, vec2 uv) {
    ivec2 texSize = textureSize(tex, 0);
    if (texSize.x <= 0 || texSize.y <= 0) {
        return textureLod(tex, uv, 0.0);
    }

    vec2 wrappedUv = uv - floor(uv);
    ivec2 baseTexel = ivec2(floor(wrappedUv * vec2(texSize)));
    baseTexel = wrapPreviewTexel(baseTexel, texSize);

    vec4 color = texelFetch(tex, baseTexel, 0);
    if (!isMagentaKeyColor(color)) {
        return color;
    }

    for (int radius = 1; radius <= 4; ++radius) {
        for (int y = -radius; y <= radius; ++y) {
            for (int x = -radius; x <= radius; ++x) {
                if (abs(x) != radius && abs(y) != radius) {
                    continue;
                }
                vec4 candidate = texelFetch(tex, wrapPreviewTexel(baseTexel + ivec2(x, y), texSize), 0);
                if (!isMagentaKeyColor(candidate)) {
                    return vec4(candidate.rgb, 0.0);
                }
            }
        }
    }

    return vec4(0.0);
}

void main() {
    if (enablePOM == PREVIEW_SIMPLE_TEXTURE_MODE) {
        vec4 texColor = samplePreviewTexture(uTexture, TexCoord);
        if (texCombiner != 0)
            texColor = combineLayers(texColor, samplePreviewTexture(uTexture2, TexCoord2), texCombiner);
        if (isMagentaKeyColor(texColor)) {
            discard;
        }
        if (alphaTest != 0 && texColor.a < 0.5) {
            discard;
        }
        if (alphaTest != 0 && hairMaterial != 0) {
            texColor.a = 1.0;
        }
        if (colorKeyBlack != 0) {
            float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            float ck = smoothstep(0.12, 0.30, lum);
            texColor.a *= ck;
            if (texColor.a < 0.01) discard;
        }
        outColor = vec4(texColor.rgb, texColor.a * opacity);
        return;
    }

    float lodFactor = computeLodFactor();
    // Gradients of the authored UV, taken here where every pixel of the quad
    // still agrees on them. Parallax moves the UV by a different amount per
    // pixel, and a mip level chosen from the moved UV flickers.
    vec2 uvDx = dFdx(TexCoord);
    vec2 uvDy = dFdy(TexCoord);

    vec3 vertexNormal = safeNormalize(Normal, vec3(0.0, 0.0, 1.0));
    if (!gl_FrontFacing) vertexNormal = -vertexNormal;

    vec2 finalUV = TexCoord;

    bool usePOM = SPEC_PARALLAX && enablePOM != 0 &&
                  alphaTest == 0 &&
                  colorKeyBlack == 0 &&
                  heightMapVariance > 0.001 &&
                  lodFactor < 0.99;
    bool useNormalMap = SPEC_NORMAL_MAP && enableNormalMap != 0 &&
                        unlit == 0 &&
                        lodFactor < 0.99 &&
                        normalMapStrength > 0.001;
    mat3 TBN;
    if (usePOM || useNormalMap) {
        vec3 T = safeNormalize(Tangent, fallbackTangent(vertexNormal));
        T = safeNormalize(T - dot(T, vertexNormal) * vertexNormal, fallbackTangent(vertexNormal));
        vec3 B = safeNormalize(Bitangent, safeNormalize(cross(vertexNormal, T), vec3(0.0, 1.0, 0.0)));
        TBN = mat3(T, B, vertexNormal);
    }

    if (usePOM) {
        mat3 TBN_inv = transpose(TBN);
        vec3 viewDirWorld = normalize(viewPos.xyz - FragPos);
        vec3 viewDirTS = TBN_inv * viewDirWorld;
        finalUV = parallaxOcclusionMap(TexCoord, viewDirTS, lodFactor);
    }

    vec4 texColor = textureGrad(uTexture, finalUV, uvDx, uvDy);
    if (texCombiner != 0)
        texColor = combineLayers(texColor, texture(uTexture2, TexCoord2), texCombiner);
    // Repair dark DXT fringes on alpha-cut character textures such as hair.
    // Transparent edge texels can carry black/garbage RGB even when alpha is
    // valid; pull color from a coarser mip and trust the source more as alpha
    // approaches opaque. This matches the generic M2 path.
    if (alphaTest != 0 && texColor.a > 0.01 && texColor.a < 1.0) {
        vec3 mipColor = textureLod(uTexture, finalUV, 4.0).rgb;
        float trust = smoothstep(0.0, 0.9, texColor.a);
        texColor.rgb = mix(mipColor, texColor.rgb, trust);
    }

    // Some classic/TBC character textures use bright magenta as a color key.
    // Apply this before any material-specific alpha path because a few preview
    // batches report as opaque/blended even when their texture still carries
    // mask-color texels.
    if (texColor.r > 0.78 && texColor.g < 0.28 && texColor.b > 0.78) {
        discard;
    }

    if (alphaTest != 0 && hairMaterial != 0) {
        if (texColor.a < 0.5) {
            discard;
        }
        texColor.a = 1.0;
    } else if (alphaTest != 0) {
        // Screen-space sharpened alpha for alpha-to-coverage anti-aliasing.
        // Rescales alpha so the 0.5 cutoff maps to exactly the texel boundary,
        // giving smooth edges when MSAA + alpha-to-coverage is active.
        float aGrad = fwidth(texColor.a);
        texColor.a = clamp((texColor.a - 0.5) / max(aGrad, 0.001) * 0.5 + 0.5, 0.0, 1.0);
        if (texColor.a < 1.0 / 255.0) discard;
    }
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        float ck = smoothstep(0.12, 0.30, lum);
        texColor.a *= ck;
        if (texColor.a < 0.01) discard;
    }

    // Compute normal (with normal mapping if enabled)
    vec3 norm = vertexNormal;
    if (useNormalMap) {
        vec3 mapNormal = textureGrad(uNormalHeightMap, finalUV, uvDx, uvDy).rgb * 2.0 - 1.0;
        mapNormal.xy *= normalMapStrength;
        mapNormal = safeNormalize(mapNormal, vec3(0.0, 0.0, 1.0));
        vec3 worldNormal = safeNormalize(TBN * mapNormal, vertexNormal);
        if (!gl_FrontFacing) worldNormal = -worldNormal;
        float blendFactor = max(lodFactor, 1.0 - normalMapStrength);
        norm = safeNormalize(mix(worldNormal, vertexNormal, blendFactor), vertexNormal);
    }

    vec3 result;

    if (unlit != 0) {
        vec3 emissiveTint = vec3(emissiveTintR, emissiveTintG, emissiveTintB);
        vec3 warm = emissiveTint * emissiveBoost;
        result = texColor.rgb * (1.0 + warm);
    } else {
        vec3 ldir = normalize(-lightDir.xyz);
        float diff = max(dot(norm, ldir), 0.0);

        vec3 viewDir = normalize(viewPos.xyz - FragPos);
        vec3 halfDir = normalize(ldir + viewDir);
        float spec = pow(max(dot(norm, halfDir), 0.0), 32.0) * specularIntensity;

        float shadow = 1.0;
        if (SPEC_SHADOWS && shadowParams.x > 0.5) {
            // Four cascades: pick the one that covers this fragment, filter it, and
            // cross-fade into the next over the last band of it. At one cascade -
            // the default, and what the client always drew - this branch is folded
            // away and the single map below is the whole of the shader.
            if (SPEC_SHADOW_CASCADES > 1) {
                shadow = mix(1.0,
                             sampleShadowCascades(uShadowMapArray, uShadowMapDepth, FragPos,
                                                  norm, ldir, length(viewPos.xyz - FragPos)),
                             shadowParams.y);
            } else {
                float normalOffset = shadowTexel() * 2.0 * (1.0 - abs(dot(norm, ldir)));
                vec3 biasedPos = FragPos + norm * normalOffset;
                vec4 lsPos = lightSpaceMatrix * vec4(biasedPos, 1.0);
                vec3 proj = lsPos.xyz / lsPos.w;
                proj.xy = proj.xy * 0.5 + 0.5;
                if (proj.x >= 0.0 && proj.x <= 1.0 &&
                    proj.y >= 0.0 && proj.y <= 1.0 &&
                    proj.z >= 0.0 && proj.z <= 1.0) {
                    float bias = max(0.0005 * (1.0 - abs(dot(norm, ldir))), 0.00005);
                    shadow = sampleShadowPCF(uShadowMap, vec3(proj.xy, proj.z - bias));
                }
                shadow = mix(1.0, shadow, shadowParams.y);
            }
        }

        result = ambientColor.rgb * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb);
    }

    if (unlit == 0) result += localLightContribution(FragPos, norm, texColor.rgb);

    float dist = length(viewPos.xyz - FragPos);
    result = applyFog(result, FragPos, dist);
    if (!finiteVec3(result)) {
        result = texColor.rgb;
    }

    outColor = vec4(result, texColor.a * opacity);
}
