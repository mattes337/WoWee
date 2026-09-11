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

// The material's second texture layer; a white 1x1 when it has none.
layout(set = 1, binding = 1) uniform sampler2D uTexture2;

layout(set = 1, binding = 2) uniform M2Material {
    int hasTexture;
    int alphaTest;
    int colorKeyBlack;
    float colorKeyThreshold;
    int unlit;
    int blendMode;
    float fadeAlpha;
    float interiorDarken;
    float specularIntensity;
    float emissiveBoost;
    float tintR;
    float tintG;
    float tintB;
    // How a two-layer material combines its layers, from the texture unit's
    // shader id; 0 means one layer. Read <layer0 op>_<layer1 op>: "Opaque" on
    // the first means its alpha is unused, "NA" on the second means the
    // second's is.
    int texCombiner;
    // ---- appended for M3a; zero on every material until the cache answers ----
    /// 1 once the generated normal map for this batch's texture has been
    /// uploaded and bound. Zero binds the flat 128,128,255 fallback, which is
    /// the surface as it was, so a material is never half-bumped.
    int enableNormalMap;
    int enablePOM;
    float pomScale;
    int pomMaxSamples;
    /// How much height the source texture had, from generateNormalHeightMap.
    /// POM on a texture with none is a march that finds the same depth at every
    /// step and costs the whole budget to answer nothing.
    float heightMapVariance;
    float normalMapStrength;
};

// The free binding of the material set: 0 and 1 are the two texture layers and
// 2 is the block above, so the generated normal/height map is 3.
layout(set = 1, binding = 3) uniform sampler2D uNormalHeightMap;

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
layout(location = 3) flat in vec3 InstanceOrigin;
layout(location = 4) in float ModelHeight;
layout(location = 5) in float vFadeAlpha;
layout(location = 8) in vec2 TexCoord2;
layout(location = 6) flat in int vSkyMode;
layout(location = 7) flat in float vHighlight;
layout(location = 9) in vec3 Tangent;
layout(location = 10) in vec3 Bitangent;

layout(location = 0) out vec4 outColor;

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

// 4x4 Bayer dither matrix (normalized to 0..1)
float bayerDither4x4(ivec2 p) {
    int idx = (p.x & 3) + (p.y & 3) * 4;
    float m[16] = float[16](
         0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
        12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
         3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
        15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    return m[idx];
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

void main() {
    // The generated normal/height map, on the doodads.
    //
    // Everything the map is read for is inside SPEC_NORMAL_MAP_EVERYWHERE,
    // which is off by default: with it frozen off this module is the one that
    // shipped, and shader_offpath_identity is what says so. The material's own
    // enableNormalMap is what turns it on per batch, and it is zero until the
    // cache has actually produced a map - so a doodad seen for the first time
    // is flat for a frame or two and then bumps, rather than flickering
    // between two lightings of the same surface.
    //
    // The two awkward shapes below - finalUV left at zero until inside the
    // branch, and the albedo fetch spelled twice - are both the off-path test.
    // Written the natural way, `vec2 finalUV = TexCoord` puts a load of
    // TexCoord above the branch, and with the constant frozen off the optimizer
    // then folds two later loads into that one: the module is two instructions
    // shorter than the one that shipped and the test says so. Two instructions
    // are not a pixel, and the test is worth more than the two lines.
    vec2 finalUV = vec2(0.0);
    float lodFactor = 1.0;
    if (SPEC_NORMAL_MAP_EVERYWHERE) {
        finalUV = TexCoord;
        if (enableNormalMap != 0) {
            lodFactor = computeLodFactor();
            if (SPEC_PARALLAX && enablePOM != 0 && heightMapVariance > 0.001 &&
                lodFactor < 0.99) {
                vec3 nPom = normalize(Normal);
                vec3 tPom = normalize(Tangent);
                vec3 bPom = normalize(Bitangent);
                vec3 viewDirTS = transpose(mat3(tPom, bPom, nPom)) *
                                 normalize(viewPos.xyz - FragPos);
                finalUV = parallaxOcclusionMap(TexCoord, viewDirTS, lodFactor);
            }
        }
    }

    vec4 texColor;
    if (SPEC_NORMAL_MAP_EVERYWHERE) {
        texColor = hasTexture != 0 ? texture(uTexture, finalUV) : vec4(1.0);
    } else {
        texColor = hasTexture != 0 ? texture(uTexture, TexCoord) : vec4(1.0);
    }
    if (texCombiner != 0)
        texColor = combineLayers(texColor, texture(uTexture2, TexCoord2), texCombiner);
    // The batch's authored colour. A glow card is painted white and coloured
    // here - Orgrimmar's bonfire carries (1.0, 0.329, 0.0) - so without it
    // every fire in the world burns white.
    texColor.rgb *= vec3(tintR, tintG, tintB);

    // Original client sky M2s carry their authored colour and alpha, and are
    // taken as they are. They are camera-centered and unlit, and must not be
    // swallowed by world-distance fog.
    //
    // This used to sit below the three discards, which meant the sky was not
    // taken as it is. The alpha test in particular rescales alpha by its own
    // screen-space derivative:
    //
    //     float aGrad = fwidth(texColor.a);
    //     texColor.a = clamp((texColor.a - alphaCutoff) / max(aGrad, 0.001) ...
    //
    // fwidth is how fast alpha changes from one pixel to the next, so it
    // changes whenever the view does - and a nebula's alpha ramp is gentle,
    // which makes the divisor tiny and the result a hard edge. Turning the
    // camera moved that edge, so Hellfire's sky flickered while the view moved
    // and stood still when it did not, on its blended layers alone. The rescale
    // is for foliage cutouts, where a hard edge is the point.
    if (vSkyMode != 0) {
        outColor = vec4(texColor.rgb, texColor.a * vFadeAlpha);
        return;
    }

    bool isFoliage = (alphaTest == 2);

    // Fix DXT fringe: transparent edge texels have garbage (black) RGB.
    // At low alpha the original RGB is untrustworthy - replace with the
    // averaged color from nearby opaque texels (high mip).  The lower
    // the alpha the more we distrust the original color.
    if (alphaTest != 0 && texColor.a > 0.01 && texColor.a < 1.0) {
        vec3 mipColor = textureLod(uTexture, TexCoord, 4.0).rgb;
        // trust = 0 at alpha 0, trust = 1 at alpha ~0.9
        float trust = smoothstep(0.0, 0.9, texColor.a);
        texColor.rgb = mix(mipColor, texColor.rgb, trust);
    }

    float alphaCutoff = 0.5;
    if (alphaTest == 2) {
        alphaCutoff = 0.4;
    } else if (alphaTest == 3) {
        alphaCutoff = 0.25;
    } else if (alphaTest != 0) {
        alphaCutoff = 0.4;
    }
    // Mip-alpha preservation: alpha mips average downward, thinning distant
    // canopies to skeletons. Boost alpha with mip level so perceived leaf
    // density stays constant with distance.
    if (isFoliage && hasTexture != 0) {
        float mip = textureQueryLod(uTexture, TexCoord).x;
        texColor.a *= 1.0 + clamp(mip, 0.0, 4.0) * 0.18;
    }
    if (alphaTest != 0) {
        // Screen-space sharpened alpha: rescale so the cutoff maps to the
        // texel boundary. With MSAA + alpha-to-coverage on the cutout
        // pipeline this dithers the edge band across samples, smoothing
        // leaf silhouettes instead of the old hard binary discard.
        float aGrad = fwidth(texColor.a);
        texColor.a = clamp((texColor.a - alphaCutoff) / max(aGrad, 0.001) * 0.5 + 0.5, 0.0, 1.0);
        if (texColor.a < 1.0 / 255.0) discard;
    }
    if (colorKeyBlack != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < colorKeyThreshold) discard;
    }
    if (blendMode == 1 && texColor.a < 0.004) discard;

    // Per-instance color variation (foliage only)
    if (isFoliage) {
        float hash = fract(sin(dot(InstanceOrigin.xy, vec2(127.1, 311.7))) * 43758.5453);
        float hueShiftR = 1.0 + (hash - 0.5) * 0.16;       // Â±8% red
        float hueShiftB = 1.0 + (fract(hash * 7.13) - 0.5) * 0.16; // Â±8% blue
        float brightness = 0.85 + hash * 0.30;               // 85â€“115%
        texColor.rgb *= vec3(hueShiftR, 1.0, hueShiftB) * brightness;
    }

    vec3 norm = normalize(Normal);
    bool foliageTwoSided = (alphaTest == 2);
    if (!foliageTwoSided && !gl_FrontFacing) norm = -norm;

    // Detail normal perturbation (foliage only) - UV-based only so wind doesn't cause flicker
    if (isFoliage) {
        float nx = sin(TexCoord.x * 12.0 + TexCoord.y * 5.3) * 0.10;
        float ny = sin(TexCoord.y * 14.0 + TexCoord.x * 4.7) * 0.10;
        norm = normalize(norm + vec3(nx, ny, 0.0));
    }

    // The map itself, in the frame the vertex carried. Blended rather than
    // replaced: strength is the player's setting and the LOD factor fades the
    // detail out with distance, which is what keeps a field of bumped doodads
    // from sparkling at the horizon.
    if (SPEC_NORMAL_MAP_EVERYWHERE && enableNormalMap != 0 && lodFactor < 0.99 &&
        normalMapStrength > 0.001) {
        vec3 T = normalize(Tangent);
        vec3 B = normalize(Bitangent);
        vec3 mapNormal = normalize(texture(uNormalHeightMap, finalUV).rgb * 2.0 - 1.0);
        vec3 worldNormal = normalize(mat3(T, B, norm) * mapNormal);
        float blend = clamp(normalMapStrength, 0.0, 1.0) * (1.0 - lodFactor);
        norm = normalize(mix(norm, worldNormal, blend));
    }

    vec3 ldir = normalize(-lightDir.xyz);
        float nDotL = dot(norm, ldir);
        float diff = foliageTwoSided ? abs(nDotL) : max(nDotL, 0.0);

    vec3 result;
    if (unlit != 0) {
        result = texColor.rgb * emissiveBoost;
        if (emissiveBoost > 1.0) {
            // Weighted by the texel's own brightness. Added flat it lit the
            // whole quad, and a glow card is black everywhere but its middle,
            // so the card's rectangle appeared as an orange panel hanging on
            // whatever was behind the fire. Black has nothing to boost.
            float emissiveWeight =
                dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
            result += vec3(0.32, 0.14, 0.025) * (emissiveBoost - 1.0) *
                      emissiveWeight;
        }
    } else {
        vec3 viewDir = normalize(viewPos.xyz - FragPos);

        float spec = 0.0;
        float shadow = 1.0;
        if (!isFoliage) {
            vec3 halfDir = normalize(ldir + viewDir);
            spec = pow(max(dot(norm, halfDir), 0.0), 32.0) * specularIntensity;
        }

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

        // Leaf subsurface scattering (foliage only) - uses stable normal, no FragPos dependency
        vec3 sss = vec3(0.0);
        if (isFoliage) {
            float backLit = max(-nDotL, 0.0);
            float viewDotLight = max(dot(viewDir, -ldir), 0.0);
            float sssAmount = backLit * pow(viewDotLight, 4.0) * 0.35 * texColor.a;
            sss = sssAmount * vec3(1.0, 0.9, 0.5) * lightColor.rgb;
        }

        // Sky-bounce ambient for foliage: upward-facing leaves catch more
        // ambient than the canopy underside, giving the crown depth instead
        // of a uniformly-lit blob.
        vec3 ambientTerm = ambientColor.rgb;
        if (isFoliage) {
            ambientTerm *= 0.82 + 0.30 * clamp(norm.z, 0.0, 1.0);
        }
        result = ambientTerm * texColor.rgb
               + shadow * (diff * lightColor.rgb * texColor.rgb + spec * lightColor.rgb)
               + sss;

        if (interiorDarken > 0.0) {
            result *= mix(1.0, 0.5, interiorDarken);
        }
    }

    // Canopy ambient occlusion (foliage only)
    if (isFoliage) {
        float normalizedHeight = clamp(ModelHeight / 18.0, 0.0, 1.0);
        float aoFactor = mix(0.55, 1.0, smoothstep(0.0, 0.6, normalizedHeight));
        result *= aoFactor;
    }

    if (unlit == 0) result += localLightContribution(FragPos, norm, texColor.rgb);

    float dist = length(viewPos.xyz - FragPos);
    float fogAmount = fogFactor(FragPos, dist);
    if (blendMode >= 3) {
        // Additive. Mixing toward the fog colour would give the card's black
        // corners the fog's colour, and additive then adds that to the scene -
        // the whole quad shows up as a lit rectangle hanging in the air, which
        // is what Orgrimmar's bonfire glow was doing to the wall behind it.
        // Distance can only take an additive contribution away.
        result *= fogAmount;
    } else {
        result = mix(fogColor.rgb, result, fogAmount);
    }

    float outAlpha = texColor.a * vFadeAlpha;
    // Cutout materials output the sharpened coverage alpha computed above -
    // alpha-to-coverage turns it into per-sample coverage for smooth edges.
    // Color-key-only materials have no meaningful texture alpha; keep them
    // opaque after the discard.
    if (colorKeyBlack != 0 && alphaTest == 0) {
        outAlpha = vFadeAlpha;
    }
    // Pressed on. The real client lifts the whole model while the button is
    // down over it, which is what says "this one, and the click landed": a
    // warm brightening rather than a tint, so a dark door reads as lit and a
    // pale one does not blow out.
    if (vHighlight > 0.0) {
        float lift = clamp(vHighlight, 0.0, 1.0);
        result = result * (1.0 + 0.6 * lift) + vec3(0.22, 0.19, 0.10) * lift;
    }

    outColor = vec4(result, outAlpha);
}
