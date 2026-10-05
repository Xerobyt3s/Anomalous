#version 460 core

#include "noise.glsl"

out vec4 fragColor;

uniform sampler2D uMask;
uniform vec3 uColor;
uniform float uIntensity;
uniform float uSeed;
uniform float uTime;
uniform float uDim;

const float kCover = 0.22;

float covered(ivec2 p) {
    return step(kCover, texelFetch(uMask, clamp(p, ivec2(0), textureSize(uMask, 0) - 1), 0).a);
}

void main() {
    if (uDim > 0.0) {
        fragColor = vec4(0.0, 0.0, 0.0, uDim);
        return;
    }
    ivec2 size = textureSize(uMask, 0);

    int cell = max(2, size.y / 360);
    ivec2 p = (ivec2(gl_FragCoord.xy) / cell) * cell + cell / 2;

    float inside = covered(p);

    float nearIn = 0.0, nearOut = 0.0;
    for (int ring = 1; ring <= 3; ++ring) {
        float w = 1.0 - float(ring - 1) / 3.0;
        for (int i = 0; i < 8; ++i) {
            float a = 6.2831853 * float(i) / 8.0 + float(ring) * 0.39;
            ivec2 o = ivec2(round(vec2(cos(a), sin(a)) * float(ring * cell)));
            float c = covered(p + o);
            nearIn = max(nearIn, c * w);
            nearOut = max(nearOut, (1.0 - c) * w);
        }
    }

    float line = max((1.0 - inside) * nearIn, inside * nearOut * 0.6);
    if (line <= 0.0 && inside <= 0.0) discard;

    float t = floor(uTime * 16.0) / 16.0;
    vec2 q = vec2(p) / float(size.y);
    float flicker = 0.7 + 0.6 * n_noise(q * 38.0 + vec2(uSeed, -t * 6.0));
    float fill = inside * (0.1 + 0.12 * n_fbm(q * 22.0 + vec2(uSeed, -t * 2.5)));

    vec3 hot = mix(uColor, vec3(1.0, 0.92, 0.7), clamp(line * flicker - 0.45, 0.0, 1.0));
    vec3 color = hot * line * flicker * 1.6 + uColor * 0.7 * fill;
    color *= uIntensity;
    if (any(isnan(color))) discard;
    fragColor = vec4(color, 0.0);
}
