// lit_common.glsl - what the four lit fragment shaders share.
//
// terrain.frag, wmo.frag, m2.frag and character.frag each ended up with their
// own copy of the shadow filter, the fog ramp and (for two of them) the
// parallax march. Include this after the PerFrame block and after the
// material's own uniform block, and the copies are gone.
//
// Parallax is only pulled in when the shader has a normal/height map bound,
// because the march reads `uNormalHeightMap`, `pomScale` and `pomMaxSamples`
// off the material block by name. Say so before including:
//
//     #define WOWEE_HAS_NORMAL_HEIGHT_MAP
//     #include "lit_common.glsl"

#ifndef WOWEE_LIT_COMMON_GLSL
#define WOWEE_LIT_COMMON_GLSL

#include "shader_features.glsl"
#include "shadow_common.glsl"
#include "fog.glsl"

#ifdef WOWEE_HAS_NORMAL_HEIGHT_MAP
#include "parallax.glsl"
#endif

#endif  // WOWEE_LIT_COMMON_GLSL
