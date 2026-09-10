#version 450

// The bright half of the glue backdrop's glow: what in the scene is bright
// enough to bloom, blurred.
//
// ModelFFX carries a `glow` on five of the glue screens - AccountLogin says
// 0.08 - and there is no glow term anywhere in the model shader. Folding it
// into the ambient colour would brighten the scene's lit surfaces, which is
// not what glow is: a glow spreads the bright parts of the picture into the
// dark ones. So it is done where a bloom is normally done, after the scene is
// drawn, by taking what is bright, blurring it, and adding it back.
//
// This pass only produces the blurred bright parts. glue_glow.frag adds them
// to the scene. Splitting it that way is not tidiness: adding is the whole
// point of a bloom, and the first version composited this texture over the
// scene through the interface's draw list, which can only alpha-blend. An
// alpha blend *replaces*, so every pixel the glow touched lost that fraction
// of the scene underneath - a bright veil with the shape of whatever passed
// the threshold, which on the login screen reads as pale rectangles over the
// sky and a washed-out frost wyrm. There is no alpha that makes a blend an
// addition, so the addition is done here, in a pass of its own.

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Push {
    // One source texel, so the taps below are a fixed blur in pixels rather
    // than a blur that changes width with the window.
    float texelX;
    float texelY;
    // How much of the scene target the scene pass actually wrote. The target
    // is rounded up to a multiple of 32 and the picture is not, so the last
    // rows and columns are cleared black and must not be blurred inwards.
    float uvU;
    float uvV;
} push;

// Above this is "bright". Blizzard's own figure is not in the file, so this
// one is taken from the scene: measured over the Northrend login backdrop its
// luminance is 0.26 at the median, 0.53 at the ninetieth percentile and 0.79
// at the ninety-ninth. A threshold here puts the light shafts, the aurora and
// the lit snow above it and leaves the rock and the dark sky below, which is
// what glows in the authored scene.
//
// It was three quarters first, which is above the ninety-fifth percentile of
// the whole picture: almost nothing passed, and the pass ran every frame and
// produced a texture that was very nearly empty.
const float kBrightThreshold = 0.55;

vec3 brightPass(vec2 uv) {
    vec3 c = texture(uScene, clamp(uv, vec2(0.0), vec2(push.uvU, push.uvV))).rgb;
    // Luminance rather than any one channel: a saturated blue shaft is bright
    // to the eye and dim in red, and thresholding on a channel would bloom
    // whichever colour the scene happens to favour.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    return c * max(lum - kBrightThreshold, 0.0) / max(1.0 - kBrightThreshold, 1e-4);
}

void main() {
    // This pass covers the written part of its own target, so its own 0..1
    // maps onto the written part of the scene's.
    vec2 uv = TexCoord * vec2(push.uvU, push.uvV);

    // A 13-tap tent, two rings around the centre. Enough spread at half
    // resolution to read as a glow rather than as a second copy of the
    // picture, and cheap enough to run every frame on a login screen.
    const vec2 o1 = vec2(push.texelX, push.texelY) * 1.5;
    const vec2 o2 = vec2(push.texelX, push.texelY) * 3.5;

    vec3 sum = brightPass(uv) * 4.0;

    sum += brightPass(uv + vec2( o1.x,  0.0)) * 2.0;
    sum += brightPass(uv + vec2(-o1.x,  0.0)) * 2.0;
    sum += brightPass(uv + vec2( 0.0,  o1.y)) * 2.0;
    sum += brightPass(uv + vec2( 0.0, -o1.y)) * 2.0;

    sum += brightPass(uv + vec2( o1.x,  o1.y));
    sum += brightPass(uv + vec2(-o1.x,  o1.y));
    sum += brightPass(uv + vec2( o1.x, -o1.y));
    sum += brightPass(uv + vec2(-o1.x, -o1.y));

    sum += brightPass(uv + vec2( o2.x,  0.0));
    sum += brightPass(uv + vec2(-o2.x,  0.0));
    sum += brightPass(uv + vec2( 0.0,  o2.y));
    sum += brightPass(uv + vec2( 0.0, -o2.y));

    outColor = vec4(sum / 20.0, 1.0);
}
