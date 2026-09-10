#version 450

// The glue backdrop's glow, as a texture drawn over the scene it came from.
//
// ModelFFX carries a `glow` on five of the glue screens - AccountLogin says
// 0.08 - and there is no glow term anywhere in the model shader. Folding it
// into the ambient colour would brighten the scene's lit surfaces, which is
// not what glow is: a glow spreads the bright parts of the picture into the
// dark ones. So it is done where a bloom is normally done, after the scene is
// drawn, by taking what is bright, blurring it, and adding it back.
//
// Added by being drawn over the backdrop rather than composited into it. The
// scene target is multisampled and this renderer's off-screen pass clears on
// begin, so a second pass over it would throw the scene away. The interface
// draws this on top instead, and the alpha written below is what makes that
// an addition rather than a veil: black contributes nothing, so the dark sky
// is untouched and only the light shafts and the aurora spread. It is the
// same trick the widget renderer already uses for additive interface art,
// noted there as "black stays invisible, which is the whole difference
// between a glow and a slab".

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Push {
    // The screen's own glow figure, straight from the markup.
    float glow;
    // One source texel, so the taps below are a fixed blur in pixels rather
    // than a blur that changes width with the window.
    float texelX;
    float texelY;
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
    vec3 c = texture(uScene, uv).rgb;
    // Luminance rather than any one channel: a saturated blue shaft is bright
    // to the eye and dim in red, and thresholding on a channel would bloom
    // whichever colour the scene happens to favour.
    float lum = dot(c, vec3(0.299, 0.587, 0.114));
    return c * max(lum - kBrightThreshold, 0.0) / max(1.0 - kBrightThreshold, 1e-4);
}

void main() {
    // A 13-tap tent, two rings around the centre. Enough spread at half
    // resolution to read as a glow rather than as a second copy of the
    // picture, and cheap enough to run every frame on a login screen.
    const vec2 o1 = vec2(push.texelX, push.texelY) * 1.5;
    const vec2 o2 = vec2(push.texelX, push.texelY) * 3.5;

    vec3 sum = brightPass(TexCoord) * 4.0;

    sum += brightPass(TexCoord + vec2( o1.x,  0.0)) * 2.0;
    sum += brightPass(TexCoord + vec2(-o1.x,  0.0)) * 2.0;
    sum += brightPass(TexCoord + vec2( 0.0,  o1.y)) * 2.0;
    sum += brightPass(TexCoord + vec2( 0.0, -o1.y)) * 2.0;

    sum += brightPass(TexCoord + vec2( o1.x,  o1.y));
    sum += brightPass(TexCoord + vec2(-o1.x,  o1.y));
    sum += brightPass(TexCoord + vec2( o1.x, -o1.y));
    sum += brightPass(TexCoord + vec2(-o1.x, -o1.y));

    sum += brightPass(TexCoord + vec2( o2.x,  0.0));
    sum += brightPass(TexCoord + vec2(-o2.x,  0.0));
    sum += brightPass(TexCoord + vec2( 0.0,  o2.y));
    sum += brightPass(TexCoord + vec2( 0.0, -o2.y));

    vec3 bloom = sum / 20.0;

    // Laid over the scene by an ordinary alpha blend, which replaces rather
    // than adds: the result is bloom*a + scene*(1-a). So the colour written
    // here has to be *brighter* than what it covers, or raising the glow makes
    // the picture darker - which is exactly what the first attempt did, and
    // measurably: at twelve times the glow the sky's mean fell from 69 to 65.
    //
    // The colour is therefore the bloom's hue at full luminance, and the alpha
    // is how much of it to lay on. Where little passed the threshold the alpha
    // is near zero and the scene is untouched; where a lot did, a bright cast
    // of its own colour is blended in, which is what a glow looks like.
    float bl = dot(bloom, vec3(0.299, 0.587, 0.114));
    vec3 hue = bl > 1e-4 ? min(bloom / bl, vec3(1.0)) : vec3(0.0);
    float a = clamp(bl * push.glow * 8.0, 0.0, 1.0);
    outColor = vec4(hue, a);
}
