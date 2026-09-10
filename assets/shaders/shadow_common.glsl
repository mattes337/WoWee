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

#endif  // WOWEE_SHADOW_COMMON_GLSL
