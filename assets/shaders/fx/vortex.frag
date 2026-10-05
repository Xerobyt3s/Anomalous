#version 460 core

#include "noise.glsl"

in vec2 vUV;

out vec4 fragColor;

uniform float uTime;
uniform float uSpin;
uniform float uFlick;
uniform float uSeed;
uniform float uFire;
uniform float uLength;
uniform float uEndFade;

void main() {
    float t = floor(uTime * 16.0) / 16.0;
    float along = vUV.y * uLength;
    vec2 c = floor(vec2(vUV.x * 64.0, along * 10.0)) / vec2(64.0, 10.0);
    float ang = c.x + t * uSpin * 0.08 + c.y * 0.9;
    vec2 q = vec2(ang * 5.0, c.y * 4.5 - t * 2.4 + uSeed);
    float n = n_fbm(q);
    float ribs = smoothstep(0.42, 0.72, n);
    float ends = mix(1.0, smoothstep(0.0, 0.12, vUV.y) * smoothstep(1.0, 0.88, vUV.y), uEndFade);
    float a = ribs * ends;

    vec3 col;
    if (uFire > 1.5) {
        col = mix(vec3(0.02, 0.3, 0.38), vec3(0.3, 1.0, 0.8), clamp(ribs * 1.05, 0.0, 1.0));
        col += vec3(0.85, 1.0, 0.95) * pow(max(ribs - 0.7, 0.0) * 2.2, 2.0);
    } else if (uFire > 0.5) {
        float heat = ribs * 1.05;
        col = mix(vec3(0.85, 0.06, 0.02), vec3(1.0, 0.48, 0.12), clamp(heat, 0.0, 1.0));
        col += vec3(1.0, 0.8, 0.5) * pow(max(heat - 0.7, 0.0) * 2.2, 2.0);
    } else {
        col = mix(vec3(0.45, 0.58, 0.66), vec3(0.92, 0.97, 1.0), ribs);
    }
    fragColor = vec4(col * a * uFlick, 0.0);
}
