#version 450

// sunshaft_composite.frag - the shafts added over the frame.
//
// Additive, in the overlay pass, before the interface: the blurred mask times
// the sun's own colour times the player's strength. LDR throughout, which is
// what the phase file asks for - the frame this adds to has already been
// tonemapped and resolved, so there is no high range left to work in.

layout(set = 0, binding = 0) uniform sampler2D uShafts;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PC {
    /// rgb = the sun's colour this frame, w = the player's strength.
    vec4 sunColorStrength;
} pc;

void main() {
    float shaft = texture(uShafts, TexCoord).r;
    outColor = vec4(pc.sunColorStrength.rgb * (shaft * pc.sunColorStrength.w), 1.0);
}
