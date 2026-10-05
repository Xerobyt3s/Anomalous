#version 460 core

#include "../fx/noise.glsl"
uniform sampler2D uC0;
uniform sampler2D uC1;
uniform sampler2D uC2;
uniform sampler2D uC3;
uniform sampler2D uC4;
uniform sampler2D uD1;
uniform sampler2D uD2;
uniform sampler2D uD3;
uniform sampler2D uD4;
uniform sampler2D uBloom;
uniform int uS;
uniform int uMode;
uniform int uFloorV;
uniform float uKn;
uniform float uGamma;
uniform float uExposure;
uniform float uContrast;
uniform float uSaturation;
uniform vec3 uTintS;
uniform vec3 uTintL;
uniform float uTintAmt;
uniform float uVignette;
uniform int uBits;
uniform int uBayer8;
uniform float uBloomStr;
uniform float uFlash;
uniform float uTime;

uniform vec3 uAura;
uniform vec3 uRushColor;
uniform vec2 uHurt;
uniform vec2 uScreenChroma;
uniform float uHazeChroma;
uniform float uHazeShimmer;
uniform float uSwirl;
const int kMaxDist = 8;
uniform int uDistCount;
uniform vec3 uDistA[kMaxDist];
uniform vec3 uDistB[kMaxDist];
uniform vec3 uDistC[kMaxDist];
uniform vec2 uDistD[kMaxDist];
uniform sampler2D uDepth;
uniform vec2 uNF;
uniform float uSplit;

out vec4 fragColor;

const float bayer[16] = float[16](
    0.0,  8.0,  2.0, 10.0,
   12.0,  4.0, 14.0,  6.0,
    3.0, 11.0,  1.0,  9.0,
   15.0,  7.0, 13.0,  5.0);
const float bayer8[64] = float[64](
     0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,
    48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
    12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,
    60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
     3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,
    51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
    15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,
    63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0);
float bay(ivec2 c) { return bayer[(c.y & 3) * 4 + (c.x & 3)] / 16.0; }

float sceneDepth(ivec2 p) {
    float d = texelFetch(uDepth, clamp(p, ivec2(0), textureSize(uDepth, 0) - 1), 0).r;
    if (d < uSplit) return 0.05;
    float ndc = (d - uSplit) / (1.0 - uSplit) * 2.0 - 1.0;
    return 2.0 * uNF.x * uNF.y / (uNF.y + uNF.x - ndc * (uNF.y - uNF.x));
}
float softness(float depth) { return 0.15 + 0.03 * depth; }

float behind(float source, float pixel) {
    if (source <= 0.0) return 1.0;
    float soft = softness(source);
    return smoothstep(source - soft, source + soft, pixel);
}

vec2 gChroma = vec2(0.0);

ivec2 distortedPixel(vec2 frag) {
    vec2 offset = vec2(0.0);
    float t = floor(uTime * 16.0) / 16.0;
    float here = sceneDepth(ivec2(frag));
    float front = 1e9;
    for (int i = 0; i < min(uDistCount, kMaxDist); ++i) {
        vec2 c = uDistA[i].xy;
        float r = max(uDistA[i].z, 1.0);
        float strength = uDistB[i].x;
        float ringRadius = uDistB[i].y;
        float seed = uDistB[i].z;
        float depth = uDistD[i].x;
        vec2 push = vec2(0.0);
        if (ringRadius < -3.5) {
            vec2 d = frag - c;
            float dist = length(d);
            float x = dist / r;
            if (x >= 1.0 || dist < 1e-3) continue;
            float pulse = 0.75 + 0.25 * sin(uTime * 2.0 + seed);
            gChroma += d / dist * strength * pulse * smoothstep(0.0, 0.3, x) * (1.0 - smoothstep(0.75, 1.0, x)) * behind(depth, here);
            continue;
        } else if (ringRadius < -2.5) {
            vec2 ab = uDistC[i].xy - c;
            float l2 = dot(ab, ab);
            float along = l2 > 1e-3 ? clamp(dot(frag - c, ab) / l2, 0.0, 1.0) : 0.0;
            vec2 d = frag - (c + ab * along);
            float dist = length(d);
            float x = dist / mix(r, max(uDistC[i].z, 1.0), along);
            if (x >= 1.0 || dist < 1e-3) continue;
            push = -d / dist * strength * (6.75 * x * (1.0 - x) * (1.0 - x));
            depth = depth > 0.0 ? mix(depth, uDistD[i].y, along) : 0.0;
        } else if (ringRadius < -1.5) {
            vec2 d = frag - c;
            float dist = length(d);
            float x = dist / r;
            if (x >= 1.0 || dist < 1e-3) continue;
            push = -d / dist * strength * (6.75 * x * (1.0 - x) * (1.0 - x));
        } else if (ringRadius < 0.0) {
            vec2 d = (frag - c) / vec2(r * 0.6, r);
            float fall = 1.0 - smoothstep(0.45, 1.0, length(d));
            if (fall <= 0.0) continue;
            vec2 q = vec2(frag.x * 0.035, frag.y * 0.022 - t * 2.2) + seed;
            vec2 n = vec2(n_fbm(q), n_fbm(q + vec2(5.2, 1.3))) - 0.5;
            push = n * vec2(1.0, 0.6) * strength * 2.0 * fall;
        } else {
            vec2 d = frag - c;
            float dist = length(d);
            float band = 1.0 - abs(dist - ringRadius) / r;
            if (band <= 0.0 || dist < 1e-3) continue;
            push = d / dist * sin(band * 3.14159265) * strength;
        }

        float w = behind(depth, here);
        if (w <= 0.001) continue;
        offset += push * w;
        if (depth > 0.0) front = min(front, depth);
    }
    ivec2 size = textureSize(uC0, 0);
    float haze = texelFetch(uC0, clamp(ivec2(frag), ivec2(0), size - 1), 0).a;
    if (haze > 0.001 && uHazeShimmer > 0.0) {
        vec2 uv = frag / vec2(size);
        vec2 wobble = vec2(sin(uv.y * 47.0 + uTime * 1.7 + sin(uv.x * 13.0 + uTime * 0.6) * 2.0),
                           cos(uv.x * 41.0 - uTime * 1.3 + sin(uv.y * 11.0 - uTime * 0.5) * 2.0));
        offset += wobble * uHazeShimmer * haze;
    }
    vec2 centre = vec2(size) * 0.5;
    vec2 fromCentre = frag - centre;
    float screenR = length(fromCentre) / float(size.y);
    if (uSwirl > 0.0) {
        vec2 d = fromCentre / vec2(size);
        float r = length(d);
        float turn = uSwirl * 1.5 * (1.0 - min(r * 1.4, 1.0));
        float sn = sin(turn);
        float cs = cos(turn);
        d = vec2(d.x * cs - d.y * sn, d.x * sn + d.y * cs) * (1.0 - uSwirl * (0.30 + 1.20 * r * r));
        offset += centre + d * vec2(size) - frag;
        gChroma += fromCentre * uSwirl * 0.02;
    }
    float screenChroma = uScreenChroma.x + uHazeChroma * haze;
    if (screenChroma > 0.0 && screenR > 1e-4) {
        float pulse = 0.75 + 0.25 * sin(uTime * 2.0 + uScreenChroma.y);
        gChroma += normalize(fromCentre) * screenChroma * pulse * smoothstep(0.05, 0.6, screenR) * (float(size.y) / 1080.0);
    }
    vec2 bent = offset;
    if (uAura.x > 0.001) {
        vec2 d = frag - vec2(size) * 0.5;
        float dist = length(d);
        if (dist > 1.0) {
            vec2 dir = d / dist;
            float edge = smoothstep(0.3, 1.0, dist / (0.5 * length(vec2(size))));
            float streak = n_noise(dir * 13.0 + vec2(t * 5.0, -t * 3.0));
            offset -= dir * edge * streak * streak * uAura.x * 0.06 * float(size.y);
        }
    }
    ivec2 sampled = clamp(ivec2(frag + offset), ivec2(0), size - 1);

    if (front < 1e8 && sceneDepth(sampled) < front - softness(front)) {
        return clamp(ivec2(frag + offset - bent), ivec2(0), size - 1);
    }
    return sampled;
}
float dth(ivec2 c) { return uBayer8 == 1 ? bayer8[(c.y & 7) * 8 + (c.x & 7)] / 64.0 : bay(c); }

uniform ivec2 uOrigin;
uniform ivec2 uOriginFull;
uniform ivec2 uShift1;

layout(r8ui, binding = 0) uniform readonly uimage2D uPrev1;
layout(r8ui, binding = 1) uniform readonly uimage2D uPrev2;
layout(r8ui, binding = 2) uniform readonly uimage2D uPrev3;
layout(r8ui, binding = 3) uniform readonly uimage2D uPrev4;
layout(r8ui, binding = 4) uniform writeonly uimage2D uNext1;
layout(r8ui, binding = 5) uniform writeonly uimage2D uNext2;
layout(r8ui, binding = 6) uniform writeonly uimage2D uNext3;
layout(r8ui, binding = 7) uniform writeonly uimage2D uNext4;
uniform int uHistoryValid;
uniform float uHysteresis;
ivec2 wrapIndex(ivec2 g, ivec2 size) { return ((g % size) + size) % size; }

float heldScale(uint had) { return uHistoryValid == 0 ? 1.0 : (had != 0u ? 1.0 - uHysteresis : 1.0 + uHysteresis); }
#define held(prev, g) heldScale(imageLoad(prev, wrapIndex(g, imageSize(prev))).r)
#define remember(next, g, passed) imageStore(next, wrapIndex(g, imageSize(next)), uvec4((passed) ? 1u : 0u))

vec3 grade(vec3 c) {
    c = max(c * exp2(uExposure), 0.0);
    c = max((c - 0.42) * uContrast + 0.42, 0.0);
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, uSaturation);
    vec3 duo = l * mix(uTintS, uTintL, smoothstep(0.05, 0.72, l));
    return mix(c, duo, uTintAmt);
}

vec3 fetchL(sampler2D t, ivec2 idx) { return texelFetch(t, min(idx, textureSize(t, 0) - 1), 0).rgb; }
float fetchD(sampler2D t, ivec2 idx) { return texelFetch(t, min(idx, textureSize(t, 0) - 1), 0).r; }

void main() {
    ivec2 p = distortedPixel(gl_FragCoord.xy);
    vec3 col;
    int blockN = 1;
    if (uMode == 2) {
        col = texelFetch(uC0, p, 0).rgb;
    } else if (uMode == 1) {
        col = fetchL(uC1, (p + uOrigin) / uS);
        blockN = uS;
    } else {
        int s1 = uS, s2 = 2 * uS, s3 = 4 * uS, s4 = 8 * uS;

        ivec2 q = p + uOrigin;
        ivec2 c1 = q / s1, c2 = q / s2, c3 = q / s3, c4 = q / s4;
        ivec2 g1 = c1 + uShift1, g2 = c2 + uShift1 / 2, g3 = c3 + uShift1 / 4, g4 = c4 + uShift1 / 8;

        float w4 = uKn * pow(fetchD(uD4, c4), -uGamma);
        float w3 = uKn * pow(fetchD(uD3, c3), -uGamma);
        float w2 = uKn * pow(fetchD(uD2, c2), -uGamma);
        float w1 = uKn * pow(fetchD(uD1, c1), -uGamma);

        float j4 = (bay(g4) - 0.5) * 0.5;
        float j3 = (bay(g3) - 0.5) * 0.5;
        float j2 = (bay(g2) - 0.5) * 0.5;
        float j1 = (bay(g1) - 0.5) * 0.5;

        bool pass4 = w4 >= float(s4) * (1.0 + j4) * held(uPrev4, g4);
        bool pass3 = w3 >= float(s3) * (1.0 + j3) * held(uPrev3, g3);
        bool pass2 = w2 >= float(s2) * (1.0 + j2) * held(uPrev2, g2);
        bool pass1 = w1 >= float(s1) * (1.0 + j1) * held(uPrev1, g1);
        remember(uNext4, g4, pass4);
        remember(uNext3, g3, pass3);
        remember(uNext2, g2, pass2);
        remember(uNext1, g1, pass1);
        if (pass4) {
            col = fetchL(uC4, c4);
            blockN = s4;
        } else if (pass3) {
            col = fetchL(uC3, c3);
            blockN = s3;
        } else if (pass2) {
            col = fetchL(uC2, c2);
            blockN = s2;
        } else if (pass1) {
            col = fetchL(uC1, c1);
            blockN = s1;
        } else if (uFloorV == 1) {
            col = fetchL(uC1, c1);
            blockN = s1;
        } else if (uFloorV == 2) {
            float t = clamp((fetchD(uD1, c1) - 4.5) / 9.5, 0.0, 1.0) * 0.5;
            if (bay(p + uOriginFull) < t) {
                col = texelFetch(uC0, p, 0).rgb;
            } else {
                col = fetchL(uC1, c1);
                blockN = s1;
            }
        } else {
            col = texelFetch(uC0, p, 0).rgb;
        }
    }

    if (dot(gChroma, gChroma) > 0.01) {
        ivec2 lim = textureSize(uC0, 0) - 1;
        ivec2 pr = clamp(ivec2(vec2(p) + gChroma), ivec2(0), lim);
        ivec2 pb = clamp(ivec2(vec2(p) - gChroma), ivec2(0), lim);

        if (uMode == 2 || blockN == 1) {
            col.r = texelFetch(uC0, pr, 0).r;
            col.b = texelFetch(uC0, pb, 0).b;
        } else if (blockN == uS) {
            col.r = fetchL(uC1, (pr + uOrigin) / blockN).r;
            col.b = fetchL(uC1, (pb + uOrigin) / blockN).b;
        } else if (blockN == 2 * uS) {
            col.r = fetchL(uC2, (pr + uOrigin) / blockN).r;
            col.b = fetchL(uC2, (pb + uOrigin) / blockN).b;
        } else if (blockN == 4 * uS) {
            col.r = fetchL(uC3, (pr + uOrigin) / blockN).r;
            col.b = fetchL(uC3, (pb + uOrigin) / blockN).b;
        } else {
            col.r = fetchL(uC4, (pr + uOrigin) / blockN).r;
            col.b = fetchL(uC4, (pb + uOrigin) / blockN).b;
        }
    }

    ivec2 wsz = textureSize(uC0, 0);
    if (uBloomStr > 0.0)
        col += texture(uBloom, (vec2(p) + 0.5) / vec2(wsz)).rgb * uBloomStr;

    vec2 ndc = vec2(p) / vec2(wsz) * 2.0 - 1.0;
    col = grade(col);
    col *= 1.0 - uVignette * smoothstep(0.55, 1.35, length(ndc));
    if (dot(uAura, vec3(1.0)) > 0.001) {
        vec2 bp = (floor(vec2(p + uOrigin) / float(blockN)) + 0.5) * float(blockN) - vec2(uOrigin);
        vec2 an = bp / vec2(wsz) * 2.0 - 1.0;
        an.x *= float(wsz.x) / float(wsz.y);
        float ar = length(an);
        vec2 adir = ar > 1e-4 ? an / ar : vec2(0.0, 1.0);
        float at = floor(uTime * 16.0) / 16.0;
        if (uAura.x > 0.001) {
            float streak = n_noise(adir * 13.0 + vec2(at * 5.0, -at * 3.0));
            float lines = smoothstep(0.72, 0.9, streak) * smoothstep(0.55, 1.5, ar);
            col += uRushColor * lines * uAura.x * 0.5;
        }
        if (uAura.y > 0.001) {
            float fog = n_fbm(an * 1.7 + vec2(at * 0.22, -at * 0.14));
            float fog2 = n_fbm(an * 3.4 - vec2(at * 0.3, at * 0.1) + 7.3);
            float amount = smoothstep(0.35, 1.25, ar * (0.75 + 0.7 * fog)) * uAura.y;
            float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
            col = mix(col, vec3(luma) * vec3(0.82, 0.95, 1.0), 0.45 * uAura.y);
            vec3 mist = mix(vec3(0.16, 0.24, 0.27), vec3(0.42, 0.56, 0.58), fog2);
            col = mix(col, mist, clamp(amount * 0.85, 0.0, 0.9));
        }
        if (uAura.z > 0.001) {
            float frame = floor(uTime * 16.0);
            float spokes = 12.0;
            float turn = atan(adir.y, adir.x) / 6.2831853 * spokes;
            float cell = floor(turn);
            float live = step(0.62, n_hash(vec2(cell, frame)));
            float wander = (n_noise(vec2(ar * 9.0 + cell * 3.1, frame * 1.7)) - 0.5) * 0.55
                         + (n_noise(vec2(ar * 31.0 - cell, frame)) - 0.5) * 0.16;
            float inCell = fract(turn) - 0.5;
            float off = abs(inCell + wander * 0.6);
            live *= smoothstep(0.5, 0.36, abs(inCell));
            float reach = 0.78 + 0.35 * n_hash(vec2(cell + 7.0, frame));
            float body = smoothstep(reach, reach + 0.35, ar);
            float line = smoothstep(0.05, 0.0, off * ar) * body * live;
            float halo = smoothstep(0.22, 0.0, off * ar) * body * live * 0.25;
            col += vec3(0.55, 0.7, 1.0) * (line * 2.2 + halo) * uAura.z + vec3(0.2, 0.3, 0.5) * smoothstep(0.9, 1.7, ar) * uAura.z * 0.5;
        }
    }
    if (uHurt.x > 0.004 || uHurt.y > 0.004) {
        vec2 hn = ((floor(vec2(p + uOrigin) / float(blockN)) + 0.5) * float(blockN) - vec2(uOrigin)) / vec2(wsz) * 2.0 - 1.0;
        float hr = length(hn * vec2(0.85, 1.0));
        float lost = uHurt.x;
        float beat = pow(0.5 + 0.5 * sin(uTime * 5.2), 6.0) * smoothstep(0.6, 0.9, lost);
        float closeIn = smoothstep(1.25 - 0.95 * lost - 0.12 * beat, 1.45 - 0.45 * lost, hr);
        float luma = dot(col, vec3(0.2126, 0.7152, 0.0722));
        col = mix(col, vec3(luma), 0.75 * lost);
        col *= 1.0 - closeIn * min(1.0, 0.25 + 1.1 * lost);
        col += vec3(0.45, 0.02, 0.02) * (uHurt.y * (0.25 + 0.75 * smoothstep(0.3, 1.2, hr)) + 0.25 * beat * closeIn);
    }
    col = mix(col, vec3(1.0), uFlash);

    if (uMode != 2) {
        float levels = exp2(float(uBits)) - 1.0;
        col += (dth((p + uOriginFull) / blockN) - 0.5) / levels;
        col = floor(col * levels + 0.5) / levels;
    }
    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
