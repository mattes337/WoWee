// fog.glsl - the distance fog every lit surface ends with.
//
// The same two lines were written out at the bottom of terrain, WMO, M2 and
// character: a linear ramp between fogParams.x and fogParams.y and a mix
// toward fogColor. M2 needs the factor rather than the mixed colour, because
// an additively blended material fades by scaling toward black instead of
// toward the fog, so both are here.
//
// SPEC_FOG_MODEL chooses the model, and 0 - the linear ramp the client has
// always had - is the default, so a pipeline built with no specialization is
// the fog that shipped. The height model is folded out of that pipeline
// entirely rather than branched around.
//
// Needs the PerFrame block in scope for `fogParams`, `fogColor`, `fogHeight`,
// `fogSunColor`, `lightDir` and `viewPos`, and shader_features.glsl for
// SPEC_FOG_MODEL.

#ifndef WOWEE_FOG_GLSL
#define WOWEE_FOG_GLSL

/// 1 at the camera, 0 at fogEnd. The linear model the client has always had.
float fogFactorLinear(float dist) {
    return clamp((fogParams.y - dist) / (fogParams.y - fogParams.x), 0.0, 1.0);
}

/// Exponential fog whose density falls off with height, so a valley fills and
/// a hilltop clears - which is what a WoW morning actually looks like and what
/// a distance-only ramp cannot express at any setting.
///
/// The closed form of the optical depth along the segment from the camera to
/// the fragment, for a density that decays exponentially with world Z:
///
///     rho(z) = rho0 * exp(-(z - h0) / H)
///     depth  = integral of rho along the ray
///            = rho0 * dist * exp(-(zA - h0)/H) * (1 - exp(-dz/H)) / (dz/H)
///
/// with the limit dz -> 0 taken directly, because the quotient is 0/0 there
/// and a camera level with what it is looking at is the common case, not a
/// corner one.
///
/// fogHeight is (base height, rho0, 1/H, aerial strength); the first three are
/// computed on the CPU from the zone's own Light.dbc fog end, so the horizon
/// lands where the linear model put it - see rendering/height_fog.hpp.
float fogFactorHeight(vec3 worldPos, float dist) {
    float baseHeight = fogHeight.x;
    float density = fogHeight.y;
    float invScale = fogHeight.z;
    if (density <= 0.0 || invScale <= 0.0) return fogFactorLinear(dist);

    float zCam = viewPos.z;
    float zFrag = worldPos.z;
    // Clamped before the exponential rather than after: a camera far below the
    // base height is a legitimate place to stand - the bottom of a mine, or
    // under the sea - and exp() of a large positive number is an infinity that
    // turns the whole screen into fog colour.
    float atCamera = exp(clamp(-(zCam - baseHeight) * invScale, -30.0, 4.0));

    float dz = zFrag - zCam;
    float depth;
    if (abs(dz) * invScale < 1e-3) {
        depth = density * atCamera * dist;
    } else {
        float t = dz * invScale;
        depth = density * dist * atCamera * (1.0 - exp(clamp(-t, -30.0, 30.0))) / t;
    }
    return clamp(exp(-max(depth, 0.0)), 0.0, 1.0);
}

/// How much of the surface survives the atmosphere between it and the camera.
float fogFactor(vec3 worldPos, float dist) {
    if (SPEC_FOG_MODEL == 1) {
        return fogFactorHeight(worldPos, dist);
    }
    return fogFactorLinear(dist);
}

/// The surface with the atmosphere in front of it.
///
/// With the height model on, the fog also carries the sun: looking toward it
/// through a long column of air, the air itself glows. That is the aerial
/// perspective half, and its colour is the zone's own sun colour off
/// Light.dbc rather than anything invented here, so no zone shifts hue.
vec3 applyFog(vec3 color, vec3 worldPos, float dist) {
    float f = fogFactor(worldPos, dist);
    vec3 result = mix(fogColor.rgb, color, f);
    if (SPEC_FOG_MODEL == 1) {
        float strength = fogHeight.w;
        if (strength > 0.0) {
            vec3 toFrag = worldPos - viewPos.xyz;
            float len = length(toFrag);
            if (len > 1e-4) {
                vec3 viewDir = toFrag / len;
                float sunAmount = max(dot(viewDir, normalize(-lightDir.xyz)), 0.0);
                // ^8 rather than a wider lobe: the glow should be a halo
                // around the sun, not a wash over the whole sky, and the
                // skybox is drawn behind this and already carries the wash.
                result += fogSunColor.rgb * (strength * pow(sunAmount, 8.0) * (1.0 - f));
            }
        }
    }
    return result;
}

#endif  // WOWEE_FOG_GLSL
