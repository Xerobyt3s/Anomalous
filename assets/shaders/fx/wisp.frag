#version 460 core

#include "noise.glsl"

in vec3 vWorld;

out vec4 fragColor;

uniform vec3 uCameraPos;
uniform vec3 uCameraForward;
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;
uniform sampler2D uSceneDepth;
uniform vec2 uNF;
uniform float uSplit;

uniform vec3 uCenter;
uniform vec3 uHeart;
uniform float uPhase;
uniform float uFlow;
uniform float uKnot;
uniform float uRadius;
uniform vec3 uTail;
uniform float uGlow;
uniform float uHeat;
uniform float uSeed;
uniform float uTime;
uniform int uSteps;
uniform float uThroughWalls;
uniform int uForm;
uniform vec3 uDeep;
uniform vec3 uHot;

vec2 rot2(vec2 v, float a) {
    float c = cos(a), s = sin(a);
    return vec2(v.x * c - v.y * s, v.x * s + v.y * c);
}

vec2 apparition(vec3 p) {
    vec3 l = (p - uCenter) / uRadius;
    l.y *= 1.25;
    float r = length(l);
    if (r > 1.3) return vec2(0.0);
    float t = uTime;
    float big = n_fbm3(l * 1.7 + vec3(uSeed, -t * 0.12, uSeed * 0.6 + t * 0.07));
    float fine = n_noise3(l * 4.2 + vec3(-t * 0.25, t * 0.18, uSeed));

    float body = 1.0 - smoothstep(0.35, 1.0, r + (big - 0.5) * 0.9);
    float density = smoothstep(0.1, 0.55, body + (fine - 0.5) * 0.25) * 1.2;
    float temperature = (1.0 - smoothstep(0.0, 0.95, r)) * 0.55 + (big - 0.5) * 0.5;
    return vec2(max(density, 0.0), clamp(temperature, 0.0, 1.0));
}

vec2 mist(vec3 p) {
    if (uForm == 1) return apparition(p);
    vec3 l = (p - uCenter) / uRadius;

    float along = dot(l, uTail);
    if (along > 0.0) l -= uTail * (along * 0.55);
    float r = length(l);
    if (r > 1.25) return vec2(0.0);

    float t = uTime;
    float fromHeart = length(p - uHeart) / uRadius;

    const float kTwist = 2.4;
    float lift = uFlow * 1.1 + t * 0.1;
    vec2 qa = rot2(l.xz, uPhase * 1.1 + kTwist * l.y + uSeed);
    vec2 qb = rot2(l.xz, uPhase * 0.6 + kTwist * 0.7 * l.y);
    float nA = n_fbm3(vec3(qa * 2.4, l.y * 1.6 - lift * 1.4) + uSeed);
    float nB = n_fbm3(vec3(qb * 1.3, l.y * 0.9 - lift * 0.7) + uSeed * 1.7);
    float n = mix(nA, nB, smoothstep(0.25, 0.95, r));
    float fine = nA;

    float gather = exp(-fromHeart * fromHeart * 16.0) * uKnot;
    float body = 1.0 - smoothstep(0.4, 1.05, r);
    float density = gather * 0.5 + smoothstep(0.32, 0.6, body + (n - 0.5) * 0.95);

    float temperature = (1.0 - smoothstep(0.1, 1.0, r)) * 0.75 + (nA - 0.5) * 0.6 + smoothstep(0.55, 0.8, nA) * 0.3 +
                        gather * 0.6;
    if (along > 0.0) density *= 1.0 - smoothstep(0.5, 1.6, along + (fine - 0.5) * 0.6);
    return vec2(max(density, 0.0), clamp(temperature, 0.0, 1.0));
}

float sceneDistance(vec3 rd) {
    if (uThroughWalls > 0.5) return 1e9;
    float d = texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (d < uSplit) return 0.0;
    float ndc = (d - uSplit) / (1.0 - uSplit) * 2.0 - 1.0;
    float viewZ = 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - ndc * (uNF.y - uNF.x));
    return viewZ / max(dot(rd, uCameraForward), 1e-4);
}

void main() {
    vec3 ro = uCameraPos;
    vec3 toFrag = vWorld - uCameraPos;
    float toFragLength = length(toFrag);
    if (toFragLength < 1e-5) discard;
    vec3 rd = toFrag / toFragLength;

    vec3 safe = mix(rd, vec3(1e-5), lessThan(abs(rd), vec3(1e-5)));
    vec3 inv = 1.0 / safe;
    vec3 t0 = (uBoxCenter - uBoxHalf - ro) * inv;
    vec3 t1 = (uBoxCenter + uBoxHalf - ro) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tNear = max(max(max(tmin.x, tmin.y), tmin.z), 0.02);
    float tFar = min(min(min(tmax.x, tmax.y), tmax.z), sceneDistance(rd));
    if (tFar <= tNear) discard;

    int steps = clamp(uSteps, 8, 64);
    float stepLen = (tFar - tNear) / float(steps);
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float t = tNear + stepLen * jitter * 0.4;

    vec3 deep = uDeep;
    vec3 cyan = uHot;
    vec3 white = mix(uHot, vec3(1.0), 0.75);

    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < steps; ++i) {
        vec3 p = ro + rd * t;
        vec2 m = mist(p);
        if (m.x > 0.004) {
            float near = m.y;
            vec3 glow = mix(deep, cyan, smoothstep(0.15, 0.6, near));
            glow = mix(glow, white, smoothstep(0.8, 1.0, near) * (0.3 + 0.6 * uHeat));
            float a = 1.0 - exp(-m.x * 6.5 * stepLen / max(uRadius, 0.1));
            color += transmittance * a * glow * (0.3 + 1.0 * near) * uGlow;
            transmittance *= 1.0 - a * 0.6;
            if (transmittance < 0.03) break;
        }
        t += stepLen;
    }

    float alpha = (1.0 - transmittance) * min(1.0, uGlow + 0.3);
    if (alpha < 0.003 || any(isnan(color)) || any(isinf(color))) discard;
    fragColor = vec4(color, alpha);
}
