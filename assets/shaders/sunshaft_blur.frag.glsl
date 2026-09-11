#version 450

// sunshaft_blur.frag - the mask smeared along the lines that leave the sun.
//
// Thirty-two taps walking from the pixel toward the sun's place on screen, each
// weighted a little less than the last. Two passes of this, the second starting
// where the first left off, reach sixty-four taps' worth of length for the cost
// of thirty-two: the classic Mitchell radial blur, and the reason the target is
// half resolution and a single channel.
//
// The step the second pass takes is the first pass's whole span, which is what
// `density` is for - the caller passes 1/32 on the first and 1 on the second.

layout(set = 0, binding = 0) uniform sampler2D uMask;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out float outMask;

layout(push_constant) uniform PC {
    vec2 sunUV;
    /// How far along the line to the sun the whole march covers, as a fraction
    /// of the distance to it.
    float density;
    /// How much each successive tap is worth. Below 1 the shaft fades along its
    /// length rather than ending in a line.
    float decay;
    /// Scales the whole result, so the second pass does not double it.
    float weight;
    float _pad0;
    float _pad1;
    float _pad2;
} pc;

void main() {
    const int kTaps = 32;

    vec2 uv = TexCoord;
    vec2 delta = (TexCoord - pc.sunUV) * (pc.density / float(kTaps));

    float sum = texture(uMask, uv).r;
    float illumination = 1.0;
    for (int i = 0; i < kTaps; ++i) {
        uv -= delta;
        illumination *= pc.decay;
        sum += texture(uMask, uv).r * illumination;
    }

    outMask = sum * pc.weight;
}
