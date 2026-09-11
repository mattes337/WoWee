// shader_features.glsl - every renderer toggle, as a specialization constant.
//
// A toggle used to be an `int` in a uniform block and an `if` in the hot loop:
// the fragment shader that ran with normal mapping off still contained the
// normal-mapping branch, still fetched the flag, and still paid for the
// divergence. There were already a dozen of those before this phase added
// five more.
//
// A specialization constant is fixed when the pipeline is created, so the
// driver folds the branch away and the variant a player is running contains
// only the code their settings asked for. The C++ side is
// `include/rendering/shader_features.hpp`, which must agree with this file
// about every id and default - `tools/shader_feature_check.py` checks that it
// does.
//
// **Every default here is what the client did before this phase.** That is the
// contract: a module compiled with no VkSpecializationInfo at all is the
// shader that shipped, which is what `shader_offpath_identity` measures.
//
// Ids 0-15 are the boolean features, one per bit of ShaderFeatures, id N for
// bit N. Ids 16 and up are the small integers, which are not bits.

#ifndef WOWEE_SHADER_FEATURES_GLSL
#define WOWEE_SHADER_FEATURES_GLSL

// ---- booleans: constant_id N is bit N of ShaderFeatures ----

/// Perturb the surface normal by the material's normal map.
/// Defaults on because the material's own `enableNormalMap` already gates it;
/// this constant is what lets a pipeline drop the branch entirely.
layout(constant_id = 0) const bool SPEC_NORMAL_MAP = true;

/// March the height map for relief (parallax occlusion mapping).
layout(constant_id = 1) const bool SPEC_PARALLAX = true;

/// Read the sun shadow map at all. Off is the "no shadows" pipeline: with the
/// white fallback bound, the sample would return 1 everywhere anyway, and this
/// is what removes the taps rather than wasting them.
layout(constant_id = 2) const bool SPEC_SHADOWS = true;

// ---- small integers: not bits ----

/// How many cascades the shadow map holds. 1 is the single orthographic map
/// the client has always drawn, and it is the default: at 1 the cascade select
/// in shadow_common.glsl folds away entirely and the module is the one that
/// shipped.
layout(constant_id = 16) const int SPEC_SHADOW_CASCADES = 1;

/// 0 = 3x3 PCF (what shipped), 1 = 16-tap rotated Poisson, 2 = PCSS on the two
/// near cascades and Poisson beyond. Only read on the cascaded path: the single
/// map keeps its 3x3, which is what makes one cascade byte-identical.
layout(constant_id = 17) const int SPEC_SHADOW_FILTER = 0;

/// 0 = the linear start/end ramp (what shipped), 1 = exponential height fog
/// with aerial perspective.
layout(constant_id = 18) const int SPEC_FOG_MODEL = 0;

#endif  // WOWEE_SHADER_FEATURES_GLSL
