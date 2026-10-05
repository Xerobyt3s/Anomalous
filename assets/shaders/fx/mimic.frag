#version 460 core

#include "noise.glsl"

in vec3 vWorld;
out vec4 fragColor;

const int kTentacles = 8;
const int kPoints = 8;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform float uSplit;
uniform vec3 uCenter;
uniform float uScale;
uniform float uTime;
uniform float uSeed;
uniform float uThrash;
uniform vec3 uPoints[kTentacles * kPoints];
uniform float uThick[kTentacles];
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float capsule(vec3 p, vec3 a, vec3 b, float ra, float rb) {
    vec3 ab = b - a;
    float h = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-8), 0.0, 1.0);
    return length(p - a - ab * h) - mix(ra, rb, h);
}

float body(vec3 p) {
    vec3 q = p - uCenter;
    float r = length(q);
    vec3 dir = q / max(r, 1e-4);
    float t = uTime * (1.2 + 3.0 * uThrash);
    float spikes = pow(max(n_noise3(dir * 5.0 + vec3(t, -t * 0.7, uSeed)), 0.0), 3.0);
    float d = r - uScale * (0.18 + 0.09 * spikes + 0.02 * sin(uTime * 4.0 + uSeed));
    for (int i = 0; i < kTentacles; ++i) {
        float thick = uThick[i];
        if (thick < 0.02) continue;
        float tentacle = 1e9;
        for (int k = 0; k < kPoints - 1; ++k) {
            float t0 = float(k) / float(kPoints - 1);
            float t1 = float(k + 1) / float(kPoints - 1);
            vec3 a = uPoints[i * kPoints + k];
            vec3 b = uPoints[i * kPoints + k + 1];

            float r0 = (mix(0.072, 0.01, pow(t0, 0.8)) + 0.009 * sin(t0 * 16.0 - uTime * 5.0 + float(i) * 1.7)) * thick;
            float r1 = (mix(0.072, 0.01, pow(t1, 0.8)) + 0.009 * sin(t1 * 16.0 - uTime * 5.0 + float(i) * 1.7)) * thick;
            tentacle = min(tentacle, capsule(p, a, b, r0, r1));
        }
        d = smin(d, tentacle, 0.13 * uScale + 1e-3);
    }
    return d;
}

vec3 normalAt(vec3 p) {
    const vec2 k = vec2(1.0, -1.0);
    const float e = 0.0015;
    return normalize(k.xyy * body(p + k.xyy * e) + k.yyx * body(p + k.yyx * e) + k.yxy * body(p + k.yxy * e) + k.xxx * body(p + k.xxx * e));
}

void main() {
    vec3 ro = uCameraPos;
    vec3 rd = normalize(vWorld - ro);
    vec3 inv = 1.0 / (abs(rd) + 1e-6) * sign(rd + 1e-9);
    vec3 t0 = (uBoxCenter - uBoxHalf - ro) * inv;
    vec3 t1 = (uBoxCenter + uBoxHalf - ro) * inv;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    float t = max(max(max(lo.x, lo.y), lo.z), 0.0);
    float tOut = min(min(hi.x, hi.y), hi.z);
    if (tOut <= t || uScale < 0.01) discard;
    bool hit = false;
    for (int i = 0; i < 96 && t < tOut; ++i) {
        float d = body(ro + rd * t);
        if (d < 0.0008) {
            hit = true;
            break;
        }
        t += max(d * 0.7, 0.0008);
    }
    if (!hit) discard;
    vec3 p = ro + rd * t;
    vec3 n = normalAt(p);

    vec3 r = reflect(rd, n);
    float facing = max(dot(n, -rd), 0.0);
    float fres = 0.04 + 0.96 * pow(1.0 - facing, 5.0);
    vec3 sky = mix(vec3(0.015, 0.016, 0.02), vec3(0.12, 0.13, 0.16), clamp(r.y * 0.5 + 0.5, 0.0, 1.0));
    float spec = pow(max(dot(r, normalize(vec3(0.3, 1.0, 0.2))), 0.0), 90.0) * 1.6 +
                 pow(max(dot(r, normalize(-rd + vec3(0.0, 0.4, 0.0))), 0.0), 60.0) * 0.5;
    vec3 sheen = (0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + facing * 6.0 + uTime * 0.4)) * pow(1.0 - facing, 3.0) * 0.12;
    vec3 color = vec3(0.006, 0.006, 0.008) + sky * fres + vec3(spec) + sheen;

    float miss = length(cross(rd, uCenter - ro));
    float nearCore = 1.0 - smoothstep(0.14, 0.33, distance(p, uCenter) / max(uScale, 0.05));
    float beat = 0.75 + 0.25 * sin(uTime * mix(2.6, 11.0, uThrash) + uSeed);
    float dotGlow = exp(-pow(miss / (0.065 * uScale + 1e-3), 2.0)) + 0.3 * exp(-pow(miss / (0.15 * uScale + 1e-3), 2.0));
    color += vec3(0.85, 0.0, 0.015) * dotGlow * nearCore * beat;
    if (any(isnan(color)) || any(isinf(color))) discard;
    vec4 clip = uViewProj * vec4(p, 1.0);
    float ndc = clamp(clip.z / max(clip.w, 1e-6), -1.0, 1.0);
    gl_FragDepth = uSplit + (ndc * 0.5 + 0.5) * (1.0 - uSplit);
    fragColor = vec4(color, 1.0);
}
