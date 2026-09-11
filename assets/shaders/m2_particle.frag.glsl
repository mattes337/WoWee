#version 450

layout(set = 1, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform Push {
    vec2 tileCount;
    int alphaKey;
    // Whether the sprite is rounded off by the falloff below. The world's
    // sprites are; an authored scene's are shaped by their texture alone.
    int edgeMask;
} push;

layout(location = 0) in vec4 vColor;
layout(location = 1) in float vTile;
layout(location = 2) in float vFogVisibility;

layout(location = 0) out vec4 outColor;

void main() {
    vec2 p = gl_PointCoord;
    float tile = floor(vTile);
    float tx = mod(tile, push.tileCount.x);
    float ty = floor(tile / push.tileCount.x);
    vec2 uv = (vec2(tx, ty) + p) / push.tileCount;
    vec4 texColor = texture(uTexture, uv);

    if (push.alphaKey != 0) {
        float lum = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < 0.05) discard;
    }

    // Soft circular falloff for point-sprite edges - a stand-in for the quad
    // the client draws, for textures whose alpha does not reach zero at the
    // border. Not for an authored scene: the login screen's frost sprites are
    // flares whose own falloff runs past the point's edge, and rounded off at
    // half their size they drew as hard-edged discs over the wyrm.
    float edge = push.edgeMask != 0 ? 1.0 - smoothstep(0.4, 0.5, length(p - 0.5)) : 1.0;
    float alpha = texColor.a * vColor.a * edge * vFogVisibility;
    // Straight colour, not premultiplied: both particle pipelines blend by
    // SRC_ALPHA - over the frame for a blended sprite, added to it for an
    // additive one, which are M2 blend modes 2 and 3 as the client has them
    // and what the ribbon shader beside this emits. Premultiplied here, the
    // blend multiplied by alpha a second time and every sprite landed at
    // alpha squared: a frost wisp at 0.3 drew at 0.09.
    vec3 rgb = texColor.rgb * vColor.rgb;
    outColor = vec4(rgb, alpha);
}
