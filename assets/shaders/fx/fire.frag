#version 460 core

#include "noise.glsl"

in vec2 vLocal;
in vec4 vP;

out vec4 fragColor;

uniform float uTime;
uniform float uCells;
uniform vec3 uHotTint;

void main() {
    float age = vP.y, seed = vP.z, heat = abs(vP.w);
    bool ghost = vP.w < 0.0;
    float t = floor(uTime * 16.0) / 16.0;
    vec2 c = floor(vLocal * uCells) / uCells;
    float r = length(vec2(c.x, c.y * 0.85));
    float disc = clamp(1.0 - r, 0.0, 1.0);
    float n = n_fbm(c * 1.7 + vec2(seed * 0.37, seed * 0.11 - t * 1.4));
    float field = disc * 0.8 + n * 0.55;
    float erode = mix(0.42, 0.98, age);
    float shape = smoothstep(erode, erode + 0.16, field);
    if (shape <= 0.002) discard;
    float core = clamp(1.0 - r * 1.35, 0.0, 1.0);
    float h = (1.0 - age) * (0.35 + 0.65 * core) * heat;
    vec3 flame = mix(vec3(0.82, 0.10, 0.02), vec3(1.0, 0.72, 0.20), smoothstep(0.15, 0.65, h)) * uHotTint;
    flame += vec3(1.0, 0.95, 0.75) * pow(max(h - 0.62, 0.0) * 2.6, 2.0);
    float smokeK = smoothstep(0.55, 0.92, age);
    vec3 smoke = vec3(0.10, 0.10, 0.09);
    vec3 col = mix(flame, smoke, smokeK);
    float body = shape * mix(0.62, 0.82, smokeK);
    float glow = shape * (1.0 - smokeK) * (0.10 + 0.28 * h);
    if (ghost) {
        vec3 cold = mix(vec3(0.02, 0.22, 0.30), vec3(0.25, 1.0, 0.78), smoothstep(0.1, 0.6, h));
        cold += vec3(0.85, 1.0, 0.95) * pow(max(h - 0.6, 0.0) * 2.0, 2.0);
        float fade = 1.0 - smoothstep(0.45, 1.0, age);
        fragColor = vec4(cold * shape * fade * (0.08 + 0.2 * h), shape * fade * 0.2);
        return;
    }
    fragColor = vec4(col * body + flame * glow * 2.0, body);
}
