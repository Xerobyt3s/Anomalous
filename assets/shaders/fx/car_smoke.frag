#version 460 core
#include "../common.glsl"
#include "../frame.glsl"
#include "../shadow.glsl"
#include "../lighting.glsl"
#include "noise.glsl"

in vec3 vWorld;
out vec4 fragColor;

const int kMaxPuffs = 64;
const int kMaxHits = 12;
const int kSteps = 22;

uniform int uPuffCount;
uniform vec4 uPuff[kMaxPuffs];
uniform vec4 uPuffLook[kMaxPuffs];
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;
uniform sampler2D uSceneDepth;
uniform vec2 uNF;
uniform float uSplit;
uniform vec3 uCameraForward;
uniform float uTime;
uniform float uDensity;

float sceneDistance(vec3 rd) {
    float d = texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (d < uSplit) return 0.0;
    float ndc = (d - uSplit) / (1.0 - uSplit) * 2.0 - 1.0;
    float viewZ = 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - ndc * (uNF.y - uNF.x));
    return viewZ / max(dot(rd, uCameraForward), 1e-4);
}

void main() {
    vec3 ro = u_cam_pos.xyz;
    vec3 toFrag = vWorld - ro;
    float toFragLength = length(toFrag);
    if (toFragLength < 1e-5) discard;
    vec3 rd = toFrag / toFragLength;

    vec3 safe = mix(rd, vec3(1e-5), lessThan(abs(rd), vec3(1e-5)));
    vec3 inv = 1.0 / safe;
    vec3 t0 = (uBoxCenter - uBoxHalf - ro) * inv;
    vec3 t1 = (uBoxCenter + uBoxHalf - ro) * inv;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(max(tmin.x, tmin.y), tmin.z), 0.02);
    float tFar = min(min(min(tmax.x, tmax.y), tmax.z), sceneDistance(rd));
    if (tFar <= tNear) discard;

    int hits[kMaxHits];
    int hitCount = 0;
    float from = tFar;
    float to = tNear;
    for (int i = 0; i < min(uPuffCount, kMaxPuffs) && hitCount < kMaxHits; ++i) {
        vec3 oc = ro - uPuff[i].xyz;
        float b = dot(oc, rd);
        float c = dot(oc, oc) - uPuff[i].w * uPuff[i].w;
        float disc = b * b - c;
        if (disc <= 0.0) continue;
        float root = sqrt(disc);
        float enter = max(-b - root, tNear);
        float leave = min(-b + root, tFar);
        if (leave <= enter) continue;
        hits[hitCount++] = i;
        from = min(from, enter);
        to = max(to, leave);
    }
    if (hitCount == 0 || to <= from) discard;

    float stepLen = max((to - from) / float(kSteps), 0.03);
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float t = from + stepLen * jitter;

    vec3 L = sun_toward();
    vec3 sky = ambient_for_normal(vec3(0.0, 1.0, 0.0));
    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    for (int s = 0; s < kSteps && t < to; ++s) {
        vec3 p = ro + rd * t;
        float density = 0.0;
        vec3 tint = vec3(0.0);
        for (int h = 0; h < hitCount; ++h) {
            int i = hits[h];
            float q = length(p - uPuff[i].xyz) / max(uPuff[i].w, 1e-3);
            float d = (1.0 - smoothstep(0.0, 1.0, q)) * uPuffLook[i].a;
            density += d;
            tint += uPuffLook[i].rgb * d;
        }
        if (density > 0.002) {
            tint /= density;
            float churn = n_fbm3(p * 2.3 + vec3(0.0, -uTime * 0.8, uTime * 0.25));
            density *= smoothstep(0.25, 0.75, churn) * 1.8;
            float sun = shadow_factor(p, 0.8);
            vec3 light = sky * 0.55 + u_sun_color_ambient.rgb * (0.12 * sun) + pointRadiance(p) * 0.25;
            float a = 1.0 - exp(-density * uDensity * stepLen);
            color += transmittance * a * tint * light;
            transmittance *= 1.0 - a;
            if (transmittance < 0.02) break;
        }
        t += stepLen;
    }

    float alpha = 1.0 - transmittance;
    if (alpha < 0.003 || any(isnan(color)) || any(isinf(color))) discard;
    fragColor = vec4(color, alpha);
}
