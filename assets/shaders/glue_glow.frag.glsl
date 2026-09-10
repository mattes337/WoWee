#version 450

// The scene with its glow added.
//
// glue_bloom.frag produced the blurred bright parts at half size; this reads
// the scene at full size and adds them, scaled by the figure the screen's
// ModelFFX asked for. Addition, not a blend: a glow only ever puts light in,
// so the dark sky comes through untouched and no pixel of the scene is
// replaced by a paler copy of itself.
//
// The result is what the interface draws. That is why this pass exists at all
// rather than the glow being laid over the backdrop by the interface: an ImGui
// draw list has one blend state and it is an alpha blend.

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom;

layout(push_constant) uniform Push {
    // The screen's own glow figure, straight from the markup. AccountLogin
    // says 0.08, and it is a fraction of the bloom rather than a gain on it:
    // the brightest parts of the login sky pass the threshold at about 0.44,
    // so 0.08 of that is some three per cent of white added where the picture
    // is brightest and nothing at all where it is dark. Subtle is correct.
    float glow;
    // How much of each target the passes before this one wrote. Both were
    // allocated rounded up to a multiple of 32 and both wrote the same
    // fraction, so one pair of scales serves them.
    float uvU;
    float uvV;
} push;

void main() {
    vec2 uv = TexCoord * vec2(push.uvU, push.uvV);
    vec3 scene = texture(uScene, uv).rgb;
    vec3 bloom = texture(uBloom, uv).rgb;
    outColor = vec4(min(scene + bloom * push.glow, vec3(1.0)), 1.0);
}
