#version 450

// sunshaft_mask.frag - where the light gets through.
//
// The first of the three passes behind S4. It reads a half-resolution copy of
// the finished scene and answers, per pixel, how much of the sun is visible
// there: bright enough to be sky rather than a surface, and near enough to the
// sun's own place on the screen to be worth blurring toward it.
//
// The scene copy is a blit of the swapchain image rather than the scene colour
// attachment, and the depth buffer is not read at all. Both are the same
// reason: this client renders the scene multisampled straight into the
// swapchain when no upscaler is running, and a multisampled depth image cannot
// be blitted and is not bound anywhere a full-screen pass can sample it.
// Brightness stands in for "the depth here is far", which for an LDR frame is
// close enough: what the mask wants is the sky between the leaves, and the sky
// is the brightest thing in frame near the sun. The cost of the substitution is
// that a white wall lit head-on near the sun also seeds a shaft, which is what
// the threshold is set high to avoid - and the threshold is one of the numbers
// the session that wrote this had no frame to check against. See the header of
// include/rendering/sun_shafts.hpp.

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out float outMask;

layout(push_constant) uniform PC {
    /// The sun's place on screen in 0..1 texture coordinates.
    vec2 sunUV;
    /// Luminance at which a pixel starts counting as sky, and the width of the
    /// ramp above it.
    float threshold;
    float softness;
    /// How far from the sun the mask still seeds anything, in screen widths.
    float radius;
    /// The lens flare's own sun visibility, so the two agree about whether the
    /// sun is there at all.
    float visibility;
    /// Aspect ratio, so the falloff around the sun is a circle on screen rather
    /// than an ellipse.
    float aspect;
    float _pad;
} pc;

void main() {
    vec3 scene = texture(uScene, TexCoord).rgb;
    float luma = dot(scene, vec3(0.2126, 0.7152, 0.0722));

    // Only what is brighter than the threshold contributes, and the ramp above
    // it is soft so a shaft fades in as a cloud thins rather than switching on.
    float bright = smoothstep(pc.threshold, pc.threshold + max(pc.softness, 0.001), luma);

    // Distance from the sun, corrected for the frame's shape.
    vec2 delta = (TexCoord - pc.sunUV) * vec2(pc.aspect, 1.0);
    float dist = length(delta);
    float near = 1.0 - smoothstep(pc.radius * 0.35, pc.radius, dist);

    outMask = bright * near * pc.visibility;
}
