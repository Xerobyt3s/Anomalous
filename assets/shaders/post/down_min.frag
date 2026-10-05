#version 460 core

uniform sampler2D uSrc;
uniform int uK;
uniform ivec2 uOrigin;
uniform int uLinearize;
uniform vec2 uNF;
uniform float uSplit;
uniform float uNearLayerDepth;

out vec4 fragColor;

float linearDepth(float d) {
    if (d < uSplit) {
        return uNearLayerDepth;
    }
    float ndc = (d - uSplit) / (1.0 - uSplit) * 2.0 - 1.0;
    return 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - ndc * (uNF.y - uNF.x));
}

void main() {
    ivec2 dst = ivec2(gl_FragCoord.xy);
    ivec2 base = dst * uK - uOrigin;
    ivec2 sz = textureSize(uSrc, 0);
    float m = 1e9;
    for (int y = 0; y < uK; ++y)
        for (int x = 0; x < uK; ++x) {
            float v = texelFetch(uSrc, clamp(base + ivec2(x, y), ivec2(0), sz - 1), 0).r;
            if (uLinearize == 1) v = linearDepth(v);
            m = min(m, v);
        }
    fragColor = vec4(m, 0.0, 0.0, 1.0);
}
