#version 460 core

#include "noise.glsl"

in vec3 vWorld;

out vec4 fragColor;

uniform vec3 uCameraPos;
uniform vec3 uCameraForward;
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;
uniform mat3 uBasis;
uniform sampler2D uSceneDepth;
uniform vec2 uNF;
uniform float uSplit;

uniform vec3 uBase;
uniform float uHeight;
uniform float uBaseRadius;
uniform float uTopRadius;
uniform float uGrow;
uniform float uThick;
uniform float uRope;
uniform float uIntensity;
uniform float uSpin;
uniform float uTwist;
uniform float uRise;
uniform float uErosion;
uniform float uBend;
uniform float uDensity;
uniform float uHeat;
uniform float uTime;
uniform float uSeed;
uniform int uSteps;

uniform float uGround[49];
uniform vec3 uGroundCenter;
uniform float uGroundSpan;

vec2 rot(vec2 v, float a) {
    float c = cos(a), s = sin(a);
    return vec2(c * v.x - s * v.y, s * v.x + c * v.y);
}

vec2 centerline(float h) {
    float bend = uBend * (1.0 + 1.5 * uRope);
    vec2 slow = vec2(sin(h * 2.6 + uTime * 0.9 + uSeed), cos(h * 2.1 + uTime * 0.7 + uSeed * 1.7));
    vec2 fast = vec2(sin(h * 6.0 - uTime * 1.9 + uSeed * 2.3), cos(h * 5.0 - uTime * 1.5));
    return (slow * (0.35 + 0.65 * h) + fast * 0.3) * bend * h;
}

float groundAt(vec2 xz) {
    vec2 g = ((xz + (transpose(uBasis) * (uBase - uGroundCenter)).xz) / max(uGroundSpan, 1e-3) + 0.5) * 6.0;
    if (any(lessThan(g, vec2(0.0))) || any(greaterThan(g, vec2(6.0)))) return -1e3;
    ivec2 i = min(ivec2(floor(g)), ivec2(5));
    vec2 f = g - vec2(i);
    float a = uGround[i.y * 7 + i.x];
    float b = uGround[i.y * 7 + i.x + 1];
    float c = uGround[(i.y + 1) * 7 + i.x];
    float d = uGround[(i.y + 1) * 7 + i.x + 1];
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

vec2 field(vec3 p) {
    float foot = groundAt(vec2(0.0));
    float py = p.y - (foot > -1.0 ? clamp(foot, -0.5, 0.5) : 0.0);
    float h = py / uHeight;
    if (h > 1.2 || p.y < -3.3) return vec2(0.0);
    float hc = clamp(h, 0.0, 1.0);

    vec2 q = p.xz - centerline(hc);
    float r = length(q);
    float R = mix(uBaseRadius, uTopRadius, pow(hc, 1.6)) * uThick;
    R *= 1.0 + 0.7 * smoothstep(0.78, 1.05, h);
    R *= 1.0 + 1.4 * exp(-max(py, 0.0) / 0.3);

    float x = r / max(R, 1e-3);

    float body = 1.0 - smoothstep(0.6, 1.1, x) - smoothstep(1.1, 1.7, x);
    float rs = length(p.xz);
    float skirtR = uTopRadius * 1.5 * (0.35 + 0.65 * uThick);
    float above = p.y - groundAt(p.xz);
    float skirt = exp(-max(above, 0.0) / 0.3) * step(-0.05, above) * (1.0 - smoothstep(0.25 * skirtR, skirtR, rs));
    skirt *= smoothstep(0.0, 0.15, uGrow) * (1.0 - uRope);
    if (body + 0.5 * abs(uErosion) <= 0.32 && skirt <= 0.02) return vec2(0.0);

    float lift = uRise * uTime;
    vec3 pa = vec3(rot(q, uSpin * uTime + uTwist * h + uSeed), p.y);
    vec3 pb = vec3(rot(q, uSpin * 0.55 * uTime + uTwist * 0.7 * h), p.y);
    float nA = n_fbm3(vec3(pa.xy * 2.6, pa.z * 1.1 - lift * 1.4) + uSeed);
    float nB = n_fbm3(vec3(pb.xy * 1.3, pb.z * 0.6 - lift * 0.7) + uSeed * 1.7);
    float n = mix(nA, nB, smoothstep(0.25, 0.95, x));

    float d0 = body + (n - 0.5) * uErosion;
    d0 -= smoothstep(0.88, 1.2, h) * 0.9;
    d0 -= smoothstep(uGrow - 0.12, uGrow + 0.02, h);
    d0 -= uRope * (0.25 + 0.5 * nB);
    float density = smoothstep(0.32, 0.6, d0) * step(-0.01, h);

    float temperature = (1.0 - smoothstep(0.2, 1.15, x)) * (1.0 - 0.28 * hc) * uHeat + (nA - 0.5) * 0.6;
    temperature += smoothstep(0.55, 0.8, nA) * 0.3;
    temperature *= 1.0 - smoothstep(0.9, 1.2, h);

    float sd = smoothstep(0.38, 0.7, skirt + (nB - 0.5) * 0.9) * 0.9;
    if (sd > density) {
        temperature = mix(temperature, 0.52 + (nA - 0.5) * 0.7, clamp((sd - density) * 3.0, 0.0, 1.0));
        density = sd;
    }
    return vec2(density, temperature);
}

vec3 fireColor(float T) {
    vec3 c = mix(vec3(0.10, 0.065, 0.045), vec3(0.82, 0.10, 0.02), smoothstep(0.12, 0.38, T));
    c = mix(c, vec3(1.0, 0.72, 0.20), smoothstep(0.38, 0.68, T));
    c += vec3(1.0, 0.95, 0.75) * pow(max(T - 0.78, 0.0) * 2.2, 2.0);
    return c * (1.0 + 1.3 * smoothstep(0.2, 0.8, T));
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
    vec3 rd = normalize(vWorld - uCameraPos);
    mat3 toLocal = transpose(uBasis);
    vec3 roBox = toLocal * (ro - uBoxCenter);
    vec3 rdLocal = toLocal * rd;
    vec3 roBase = toLocal * (ro - uBase);

    vec3 inv = 1.0 / rdLocal;
    vec3 t0 = (-uBoxHalf - roBox) * inv;
    vec3 t1 = (uBoxHalf - roBox) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tNear = max(max(max(tmin.x, tmin.y), tmin.z), 0.02);
    float tFar = min(min(min(tmax.x, tmax.y), tmax.z), sceneDistance(rd));
    if (tFar <= tNear) discard;

    int steps = clamp(uSteps, 8, 96);
    float stepLen = max((tFar - tNear) / float(steps), 0.06);

    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float t = tNear + stepLen * jitter;

    vec3 color = vec3(0.0);
    float transmittance = 1.0;
    for (int i = 0; i < steps && t < tFar; ++i) {
        vec2 f = field(roBase + rdLocal * t);
        if (f.x > 0.003) {
            float a = 1.0 - exp(-f.x * uDensity * stepLen);
            color += transmittance * a * fireColor(f.y);
            transmittance *= 1.0 - a;
            if (transmittance < 0.02) break;
        }
        t += stepLen;
    }

    float alpha = (1.0 - transmittance) * uIntensity;
    color *= uIntensity;
    if (alpha < 0.003 || any(isnan(color))) discard;
    fragColor = vec4(color, alpha);
}
