// shadow_common.glsl - how every lit surface reads the sun shadow map.
//
// Four fragment shaders sampled the shadow map - terrain, WMOs, M2 doodads and
// characters - and all four carried their own copy of these two functions,
// character for character. A bias changed in one of them was a bias changed in
// one of them. Include this instead; it needs the PerFrame block in scope for
// `shadowParams`, so it goes after that declaration.
//
// The filter is chosen by a specialization constant rather than a uniform, so
// the pipeline the driver compiles has exactly one filter in it and the other
// two are not in the binary at all. SHADOW_FILTER 0 is the 3x3 PCF this always
// had, and it is the default: an unspecialized module is today's shader.
//
// The same is true of the cascade count. At 1 the caller uses its own single
// map through `uShadowMap` and nothing below the divider is reachable, so the
// compiler folds all of it away and the module is byte for byte the one that
// shipped - which is what tools/shader_offpath_check.py measures. The cascaded
// path reads a second binding, `uShadowMapArray`, rather than changing the
// type of the first: changing it would have changed the single-cascade SPIR-V
// too, and then nothing could have said the off path was untouched.

#ifndef WOWEE_SHADOW_COMMON_GLSL
#define WOWEE_SHADOW_COMMON_GLSL

// One texel of the shadow map, handed in by the renderer. The map is 512,
// 1024, 2048 or 4096 a side by the quality setting; this used to be a
// constant for 4096, so at 512 the filter taps all landed inside one texel
// and the bias shrank eightfold. The fallback covers a per-frame block that
// never filled the slot in, such as the character preview's.
float shadowTexel() {
    return shadowParams.z > 0.0 ? shadowParams.z : 1.0 / 4096.0;
}

float sampleShadowPCF(sampler2DShadow smap, vec3 coords) {
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(smap, vec3(coords.xy + vec2(x, y) * shadowTexel(), coords.z));
        }
    }
    return shadow / 9.0;
}

// ---- cascaded shadows -------------------------------------------------
//
// Everything below is reachable only when SPEC_SHADOW_CASCADES is greater
// than 1. At the default of 1 the constant folds and the whole of it is
// removed before the driver ever sees it.

/// Sixteen points on a disc, low discrepancy. The same set the Poisson filter
/// samples and the PCSS blocker search walks, so a penumbra estimated from the
/// blockers is estimated from the taps that will be shaded.
const vec2 kShadowPoisson16[16] = vec2[](
    vec2(-0.94201624, -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.09418410, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,  0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,  0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325, -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911, -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,  0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,  0.78641367), vec2( 0.14383161, -0.14100790));

/// Interleaved-gradient noise, Jorge Jimenez's. One multiply-add and a fract
/// per pixel, and it decorrelates the rotation across a 3x3 neighbourhood
/// better than a hashed random does - which is what stops sixteen taps from
/// reading as sixteen taps.
float shadowNoise(vec2 pixel) {
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

mat2 shadowTapRotation() {
    float a = shadowNoise(gl_FragCoord.xy) * 6.2831853;
    float s = sin(a);
    float c = cos(a);
    return mat2(c, -s, s, c);
}

/// How wide one cascade's map is in the world, read back out of its own
/// matrix. The ortho projection scales world yards to clip space by
/// 1/halfExtent along each axis, so the length of the matrix's first column
/// through the rotation is that reciprocal. Reading it here rather than
/// sending a fifth vec4 keeps PerFrame the size the last phase settled.
float cascadeHalfExtent(int c) {
    vec3 col = vec3(cascadeMatrix[c][0][0], cascadeMatrix[c][1][0], cascadeMatrix[c][2][0]);
    float len = length(col);
    return len > 1e-8 ? 1.0 / len : 1.0;
}

float sampleShadowPCFArray(sampler2DArrayShadow smap, int c, vec3 coords) {
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(smap, vec4(coords.xy + vec2(x, y) * shadowTexel(), float(c), coords.z));
        }
    }
    return shadow / 9.0;
}

/// Sixteen rotated taps over `radius` texels. The rotation is per pixel, so
/// the banding a fixed kernel leaves along a shadow edge becomes noise that
/// the eye reads as a soft edge instead.
float sampleShadowPoissonArray(sampler2DArrayShadow smap, int c, vec3 coords, float radius) {
    mat2 rot = shadowTapRotation();
    float texel = shadowTexel();
    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rot * kShadowPoisson16[i] * radius * texel;
        shadow += texture(smap, vec4(coords.xy + offset, float(c), coords.z));
    }
    return shadow / 16.0;
}

/// Percentage-closer soft shadows: find how far in front of this fragment the
/// things casting on it are, and widen the filter by that much.
///
/// The blocker search needs the depth itself rather than a comparison against
/// it, which a sampler2DArrayShadow cannot answer - hence uShadowMapDepth,
/// the same image through an ordinary sampler. It is only ever read here.
float sampleShadowPCSSArray(sampler2DArrayShadow smap, sampler2DArray depths,
                            int c, vec3 coords, float lightSizeUV) {
    mat2 rot = shadowTapRotation();
    // Search over the light's own width: nothing outside it can cast a
    // penumbra onto this point.
    float searchRadius = max(lightSizeUV, shadowTexel());
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec2 offset = rot * kShadowPoisson16[i] * searchRadius;
        float d = texture(depths, vec3(coords.xy + offset, float(c))).r;
        if (d < coords.z) {
            blockerSum += d;
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5) return 1.0;  // nothing in front: fully lit

    float blockerDepth = blockerSum / blockerCount;
    // Similar triangles, in the cascade's own depth range. Both depths are
    // the same normalized units, so the ratio needs no unit conversion.
    float penumbra = (coords.z - blockerDepth) / max(blockerDepth, 1e-5);
    float radiusUV = clamp(penumbra * lightSizeUV, shadowTexel(), lightSizeUV * 4.0);
    return sampleShadowPoissonArray(smap, c, coords, radiusUV / shadowTexel());
}

/// Which cascade covers a fragment this far down the view axis.
int selectCascade(float viewDepth, int count) {
    for (int i = 0; i < count - 1; ++i) {
        if (viewDepth < shadowSplits[i]) return i;
    }
    return count - 1;
}

/// One cascade's answer for one fragment, filter and all. Returns 1 where the
/// fragment falls outside the cascade, so the caller's blend can lean on the
/// next one instead of on a hard edge.
float shadowInCascade(sampler2DArrayShadow smap, sampler2DArray depths,
                      int c, vec3 fragPos, vec3 norm, vec3 ldir) {
    // The same normal offset and slope-scaled bias the single map has always
    // used. The offset is in texels of this cascade, which are wider the
    // further out it reaches - which is exactly the scale acne appears at.
    float halfExtent = cascadeHalfExtent(c);
    float texelWorld = (2.0 * halfExtent) * shadowTexel();
    float slope = 1.0 - abs(dot(norm, ldir));
    vec3 biasedPos = fragPos + norm * (texelWorld * 2.0 * slope);
    vec4 lsPos = cascadeMatrix[c] * vec4(biasedPos, 1.0);
    vec3 proj = lsPos.xyz / lsPos.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0 ||
        proj.z < 0.0 || proj.z > 1.0) {
        return 1.0;
    }
    float bias = max(0.0005 * slope, 0.00005);
    vec3 coords = vec3(proj.xy, proj.z - bias);

    if (SPEC_SHADOW_FILTER == 1) {
        return sampleShadowPoissonArray(smap, c, coords, 2.0);
    } else if (SPEC_SHADOW_FILTER == 2) {
        // PCSS on the two cascades near the camera, where a penumbra is worth
        // sixteen extra taps; Poisson beyond, where the blocker search would
        // be measuring a shadow four pixels wide.
        if (c <= 1) {
            float lightSizeUV = shadowParams.w / max(2.0 * halfExtent, 1e-3);
            return sampleShadowPCSSArray(smap, depths, c, coords, lightSizeUV);
        }
        return sampleShadowPoissonArray(smap, c, coords, 2.0);
    }
    return sampleShadowPCFArray(smap, c, coords);
}

/// The whole cascaded read: pick a cascade, filter it, and cross-fade into the
/// next one over the last few yards of this one so the change of resolution is
/// not a line drawn across the ground.
float sampleShadowCascades(sampler2DArrayShadow smap, sampler2DArray depths,
                           vec3 fragPos, vec3 norm, vec3 ldir, float viewDepth) {
    int count = clamp(shadowMeta.x, 1, SPEC_SHADOW_CASCADES);
    int c = selectCascade(viewDepth, count);
    float shadow = shadowInCascade(smap, depths, c, fragPos, norm, ldir);

    float band = float(shadowMeta.y);
    if (c + 1 < count && band > 0.0) {
        float toEdge = shadowSplits[c] - viewDepth;
        if (toEdge < band) {
            float next = shadowInCascade(smap, depths, c + 1, fragPos, norm, ldir);
            shadow = mix(next, shadow, clamp(toEdge / band, 0.0, 1.0));
        }
    }
    return shadow;
}

#endif  // WOWEE_SHADOW_COMMON_GLSL
