#version 460 core

#include "noise.glsl"

in vec2 vUv;
in float vPart;

out vec4 fragColor;

uniform float uTime;
uniform float uIntensity;

vec3 fireRamp(float h) {
    vec3 c = mix(vec3(0.82, 0.10, 0.02), vec3(1.0, 0.72, 0.20), smoothstep(0.15, 0.65, h));
    c += vec3(1.0, 0.95, 0.75) * pow(max(h - 0.62, 0.0) * 2.6, 2.0);
    return c;
}

void main() {
    float t = floor(uTime * 16.0) / 16.0;
    if (vPart < 0.5) {
        vec2 c = vec2(floor(vUv.x * 24.0) / 24.0, floor(vUv.y * 14.0) / 14.0);
        float tongues = n_fbm(vec2(c.x * 2.2, c.y * 1.6 - t * 3.2));
        float licks = n_fbm(vec2(c.x * 5.5 + 7.0, c.y * 3.0 - t * 5.0));

        float body = (1.0 - c.y) * 1.25 + (tongues - 0.5) * 1.3 + (licks - 0.5) * 0.5 - 0.42;
        float shape = smoothstep(0.0, 0.18, body);
        if (shape <= 0.003) discard;
        float heat = clamp(body * 1.1 + (1.0 - c.y) * 0.35, 0.0, 1.4);
        vec3 flame = fireRamp(heat);
        float alpha = shape * 0.55;
        fragColor = vec4(flame * shape * (0.22 + 0.4 * heat) * uIntensity, alpha * min(uIntensity, 1.0));
    } else {
        vec2 c = vec2(floor(vUv.x * 12.0) / 12.0, floor(vUv.y * 16.0) / 16.0);
        float fall = (1.0 - c.y);
        fall *= fall;
        float embers = n_fbm(vec2(c.x * 3.0, c.y * 5.0 + t * 0.8));
        float glow = fall * (0.35 + 0.9 * smoothstep(0.35, 0.75, embers)) * smoothstep(0.0, 0.08, 1.0 - c.y);
        vec3 color = mix(vec3(0.7, 0.08, 0.01), vec3(1.0, 0.55, 0.12), fall);
        fragColor = vec4(color * glow * 0.45 * uIntensity, 0.0);
    }
}
