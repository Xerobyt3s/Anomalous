#version 460 core

#include "noise.glsl"

in vec2 vUv;
out vec4 fragColor;

uniform int uPart;
uniform float uTime;
uniform float uFade;
uniform float uBrightness;
uniform float uSeed;
uniform vec2 uLead;
uniform float uStretch;
uniform float uTailFade;

const vec3 kCold = vec3(0.5, 0.78, 1.0);
const vec3 kViolet = vec3(0.62, 0.42, 1.0);

void main() {
    float t = floor(uTime * 16.0) / 16.0;
    vec3 glow = vec3(0.0);
    float dark = 0.0;

    if (uPart == 0) {
        vec2 p = vUv;
        float r = length(p);

        dark = 0.8 * (1.0 - smoothstep(0.8, 1.02, r));

        float rim = exp(-pow((r - 0.95) / 0.13, 2.0));
        vec2 dir = r > 1e-4 ? p / r : vec2(0.0);
        float lead = 0.55 + 0.45 * dot(dir, uLead);
        float shimmer = 0.75 + 0.5 * n_noise(dir * 2.5 + vec2(t * 3.0, uSeed));
        glow += mix(kViolet, kCold, lead) * rim * lead * shimmer * 1.1;

        glow += kViolet * exp(-r * r * 0.9) * 0.1;

        float twinkle = 0.45 + 0.55 * n_hash(vec2(floor(uTime * 16.0), uSeed));
        vec2 a = abs(p);
        float star = exp(-a.y * 7.0) * exp(-a.x * 1.1) + exp(-a.x * 7.0) * exp(-a.y * 1.1);
        glow += kCold * star * twinkle * 0.7;

        float edge = 1.0 - smoothstep(2.6, 3.4, max(a.x, a.y));
        glow *= edge;
    } else {
        float u = clamp(vUv.x, 0.0, 1.0);
        float v = vUv.y;
        float width = pow(1.0 - u, 1.4);
        float e = abs(v) / max(width, 1e-3);
        if (e >= 1.0) discard;

        vec2 q = vec2(u * uStretch * 0.55 - t * 4.0, v * 2.2 + uSeed);
        float wisps = n_fbm(q);
        float along = pow(1.0 - u, 1.3);

        float sides = smoothstep(0.35, 0.85, e) * (1.0 - smoothstep(0.85, 1.0, e));
        float body = (1.0 - e) * smoothstep(0.25, 0.75, wisps);
        glow += mix(kViolet, kCold, e) * (sides * 0.8 + body * 0.3) * along * (0.45 + wisps);
        dark = 0.4 * along * (1.0 - e * e);
        glow *= uTailFade;
        dark *= uTailFade;
    }

    glow *= uBrightness * uFade;
    dark = clamp(dark * uFade, 0.0, 1.0);

    fragColor = vec4(glow + vec3(0.012, 0.008, 0.035) * dark, dark);
}
