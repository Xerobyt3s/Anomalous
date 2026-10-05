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
uniform float uRadius;
uniform float uHeight;
uniform float uAge;
uniform float uLifetime;
uniform float uElectrified;
uniform float uSeed;
uniform float uBall;
uniform float uReach[14];
uniform vec3 uPullPoint;
uniform float uPull;
uniform float uPullReach;
uniform float uPullPhase;

const int kMaxHoles = 16;
uniform int uHoleCount;
uniform vec3 uHoleA[kMaxHoles];
uniform float uHoleReach[kMaxHoles];
uniform vec3 uHoleB[kMaxHoles];

const int kMaxLights = 8;
uniform int uPointCount;
uniform vec3 uPointPos[kMaxLights];
uniform vec3 uPointColor[kMaxLights];
uniform vec3 uSunDir;

uniform float uTime;
uniform int uSteps;
uniform float uDensity;
uniform float uNoiseScale;
uniform float uChurn;
uniform float uBrightness;
uniform vec3 uTint;

const float kGrowTime = 1.0;
const float kFadeTime = 3.0;

float segmentDistance(vec3 p, vec3 a, vec3 b) {
    vec3 d = b - a;
    float l2 = dot(d, d);
    if (l2 < 1e-8) return distance(p, a);
    return distance(p, a + d * clamp(dot(p - a, d) / l2, 0.0, 1.0));
}

const float kSpill = 0.2;
float reachToward(vec3 dir) {
    float weights = 0.0;
    float inverse = 0.0;
    for (int i = 0; i < 14; ++i) {
        vec3 axis;
        if (i < 6) {
            axis = vec3(0.0);
            axis[i / 2] = (i % 2 == 0) ? 1.0 : -1.0;
        } else {
            int k = i - 6;
            axis = normalize(vec3((k & 1) != 0 ? 1.0 : -1.0, (k & 2) != 0 ? 1.0 : -1.0, (k & 4) != 0 ? 1.0 : -1.0));
        }
        float a = max(dot(dir, axis), 0.0);
        float a2 = a * a;
        float w = a2 * a2 * a2 * a2;
        weights += w;
        inverse += w / max(uReach[i], 1e-3);
    }
    return weights > 1e-6 ? weights / max(inverse, 1e-6) : 1e4;
}
float walls(vec3 p) {
    vec3 local = p - uCenter;
    float dist = length(local);
    if (dist < 1e-4) return 1.0;
    float reach = reachToward(local / dist);
    return 1.0 - smoothstep(reach - kSpill, reach, dist);
}

float shape(vec3 p, float wobble) {
    vec3 local = p - uCenter;
    float grow = smoothstep(0.0, kGrowTime, uAge);
    float size = 0.3 + 0.7 * grow;
    float rx = length(local.xz) / (uRadius * size);
    float ry = abs(local.y) / (uHeight * size);
    float e = sqrt(rx * rx + ry * ry);
    return 1.0 - smoothstep(0.45, 1.0, e + wobble);
}

float presence() {
    float fade = clamp((uLifetime - uAge) / kFadeTime, 0.0, 1.0);
    return min(1.0, uAge / 0.25) * fade;
}

void dragged(vec3 p, out vec3 shapeAt, out vec3 noiseAt, out float burn) {
    shapeAt = p;
    noiseAt = p;
    burn = 1.0;
    if (uPull <= 0.001) return;
    vec2 d = p.xz - uPullPoint.xz;
    float r = length(d);
    if (r < 1e-3) { burn = 1.0 - uPull; return; }
    float near = clamp(1.0 - r / max(uPullReach, 0.1), 0.0, 1.0);
    float n2 = near * near;
    float r0 = r + uPull * uPullReach * 0.45 * near;
    float angle = uPull * 2.4 * n2;
    float y0 = uPullPoint.y + (p.y - uPullPoint.y) / (1.0 + uPull * 1.6 * n2);
    vec2 dir = d / r;
    float c = cos(angle), s = sin(angle);
    shapeAt = vec3(uPullPoint.x + (dir.x * c - dir.y * s) * r0, y0, uPullPoint.z + (dir.x * s + dir.y * c) * r0);
    float flowAngle = angle + uPullPhase * 0.55;
    float fc = cos(flowAngle), fs = sin(flowAngle);
    float flowR = r0 + uPullPhase * 1.3;
    noiseAt = vec3(uPullPoint.x + (dir.x * fc - dir.y * fs) * flowR, y0 - uPullPhase * 0.5 * near,
                   uPullPoint.z + (dir.x * fs + dir.y * fc) * flowR);
    burn = mix(1.0, smoothstep(0.5, 2.4, r), uPull);
}

vec2 fog(vec3 p) {
    float t = uTime * uChurn;

    vec3 shapeAt, noiseAt;
    float burn;
    dragged(p, shapeAt, noiseAt, burn);
    vec3 q = noiseAt * uNoiseScale + vec3(uSeed, -t * 0.12, uSeed * 0.7 + t * 0.08);
    float big = n_fbm3(q);
    float fine = n_noise3(noiseAt * uNoiseScale * 3.1 + vec3(-t * 0.3, t * 0.22, uSeed));
    float d = shape(shapeAt, (big - 0.5) * 1.0 + (fine - 0.5) * 0.18) * burn * walls(p);
    if (d <= 0.0) return vec2(0.0, fine);
    d *= smoothstep(0.12, 0.7, big) * 1.25;
    d = clamp(d + (fine - 0.5) * 0.35 * (1.0 - d), 0.0, 1.0);

    for (int i = 0; i < min(uHoleCount, kMaxHoles); ++i) {
        float reach = uHoleReach[i];
        if (reach <= 0.0) continue;
        float dist = segmentDistance(p, uHoleA[i], uHoleB[i]) + (fine - 0.5) * reach * 0.6;
        d *= smoothstep(reach * 0.4, reach, dist);
    }
    return vec2(d * presence(), fine);
}

float sceneDistance(vec3 rd) {
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

    int steps = clamp(uSteps, 8, 96);
    float stepLen = max((tFar - tNear) / float(steps), 0.07);

    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float t = tNear + stepLen * jitter;

    float flicker = 0.55 + 0.45 * n_hash(vec2(floor(uTime * 16.0), uSeed));
    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < steps && t < tFar; ++i) {
        vec3 p = ro + rd * t;
        vec2 f = fog(p);
        if (f.x > 0.004) {
            float toSun = shape(p + uSunDir * 0.5, 0.0) + shape(p + uSunDir * 1.3, 0.0);
            float shadow = exp(-toSun * 1.5 * presence());
            float high = clamp((p.y - uCenter.y) / max(uHeight, 0.1), 0.0, 1.0);
            vec3 light = vec3(0.09, 0.115, 0.14) * (0.5 + 0.5 * high)
                       + vec3(0.46, 0.5, 0.55) * shadow * (0.6 + 0.8 * f.y);
            vec3 lamps = vec3(0.0);
            for (int l = 0; l < min(uPointCount, kMaxLights); ++l) {
                vec3 toLight = uPointPos[l] - p;
                float d2 = max(dot(toLight, toLight), 1e-4);
                lamps += uPointColor[l] * min(1.0 / (0.6 + 0.32 * d2), 1.6) * 0.1;
            }

            light += lamps / (1.0 + dot(lamps, vec3(0.6)));
            if (uElectrified > 0.001) {
                float vein = smoothstep(0.82, 1.0, 1.0 - abs(2.0 * f.y - 1.0));
                light += vec3(0.3, 0.5, 1.0) * uElectrified * flicker * (0.22 + 2.2 * vein);
            }
            float a = 1.0 - exp(-f.x * uDensity * stepLen);
            color += transmittance * a * uTint * light * uBrightness;
            transmittance *= 1.0 - a;
            if (transmittance < 0.02) break;
        }
        t += stepLen;
    }

    float alpha = 1.0 - transmittance;
    if (alpha < 0.003 || any(isnan(color)) || any(isinf(color))) discard;
    fragColor = vec4(color, alpha);
}
