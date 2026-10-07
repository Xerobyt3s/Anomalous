#version 460 core

#include "noise.glsl"

in vec3 vWorld;

out vec4 fragColor;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;
uniform float uSplit;
uniform vec3 uLightDir;
uniform vec3 uSunColor;
uniform float uTime;

uniform vec3 uBody;
uniform vec3 uBodyRadii;
uniform mat3 uBodyBasis;
uniform float uStir;
uniform float uSeed;
uniform float uBlink;
uniform vec3 uHead;
uniform float uHeadRadius;
uniform vec3 uNeck;
uniform vec3 uHeadForward;
uniform vec3 uHeadUp;
uniform vec3 uArms[12];
uniform vec3 uLegs[6];
uniform vec3 uFootForward[2];
uniform vec3 uSkin;
uniform float uDissolve;
uniform float uCowboy;
uniform vec3 uHem[12];

const int kSteps = 96;
const float kArmRadius = 0.062;
const float kHandRadius = 0.075;

const vec3 kJacket = vec3(0.42, 0.27, 0.16);
const vec3 kJacketEdge = vec3(0.3, 0.19, 0.11);
const vec3 kTee = vec3(0.86, 0.86, 0.82);
const vec3 kJeans = vec3(0.24, 0.32, 0.5);
const vec3 kBoot = vec3(0.3, 0.19, 0.12);
const vec3 kInk = vec3(0.06, 0.03, 0.1);

const float kSkin = 0.0;
const float kTop = 1.0;
const float kBottom = 2.0;
const float kSleeve = 3.0;
const float kBoots = 4.0;
const float kHat = 5.0;
const float kHatBand = 6.0;
const float kPoncho = 7.0;

const vec3 kHatFelt = vec3(0.52, 0.35, 0.19);
const vec3 kHatRibbon = vec3(0.12, 0.07, 0.04);
const vec3 kPonchoRed = vec3(0.6, 0.17, 0.11);
const vec3 kPonchoCream = vec3(0.88, 0.8, 0.62);
const vec3 kPonchoBrown = vec3(0.28, 0.16, 0.09);
const vec3 kPonchoTeal = vec3(0.12, 0.38, 0.4);

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float taper(vec3 p, vec3 a, vec3 b, float ra, float rb, out float along) {
    vec3 pa = p - a;
    vec3 ba = b - a;
    along = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * along) - mix(ra, rb, along);
}

float ellipsoid(vec3 p, vec3 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / max(k1, 1e-6);
}

float roundBox(vec3 p, vec3 extent, float r) {
    vec3 q = abs(p) - (extent - vec3(r));
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r;
}

vec3 toBody(vec3 p) { return transpose(uBodyBasis) * (p - uBody); }

vec2 torso(vec3 p) {
    vec3 q = toBody(p);
    float low = clamp((-q.y) / uBodyRadii.y, 0.0, 1.0);

    vec3 extent = uBodyRadii * vec3(1.0 - low * 0.1, 1.0, 1.0 - low * 0.04);
    float d = roundBox(q, extent, min(0.12, uBodyRadii.z * 0.95));
    float wobble = n_noise3(p * 6.0 + vec3(uSeed, uTime * (0.8 + uStir * 3.0), uSeed * 0.7)) - 0.5;
    d += wobble * (0.003 + uStir * 0.015);

    for (int s = -1; s <= 1; s += 2) {
        vec3 c = vec3(float(s) * (uBodyRadii.x + 0.02), uBodyRadii.y - 0.13, 0.0);
        d = smin(d, ellipsoid(q - c, vec3(0.085, 0.085, 0.095)), 0.05);
    }
    float waist = -uBodyRadii.y + 0.24;
    return vec2(d, q.y < waist ? 1.0 : 0.0);
}

vec2 arms(vec3 p) {
    float along;
    float d = 1e9;
    float hands = 1e9;
    for (int a = 0; a < 2; ++a) {
        for (int i = 0; i < 5; ++i) {
            d = min(d, taper(p, uArms[a * 6 + i], uArms[a * 6 + i + 1], kArmRadius, kArmRadius * 0.92, along));
        }

        vec3 hand = uArms[a * 6 + 5];
        vec3 dir = normalize(uArms[a * 6 + 5] - uArms[a * 6 + 4] + vec3(0.0, -1e-4, 0.0));
        float mitt = taper(p, hand - dir * 0.02, hand + dir * 0.05, kHandRadius, kHandRadius * 0.85, along);
        hands = min(hands, mitt);
        d = smin(d, mitt, 0.02);
    }
    return vec2(d, hands);
}

vec3 legs(vec3 p) {
    float along;
    float d = 1e9;
    float material = kBottom;
    float boots = 1e9;
    for (int l = 0; l < 2; ++l) {
        vec3 hip = uLegs[l * 3];
        vec3 knee = uLegs[l * 3 + 1];
        vec3 foot = uLegs[l * 3 + 2];

        vec3 flatFwd = normalize(uFootForward[l] + vec3(0.0, 0.0, 1e-4));

        vec3 bootUp = normalize(mix(vec3(0.0, 1.0, 0.0), uBodyBasis[1], smoothstep(0.55, 0.95, abs(flatFwd.y))) + vec3(1e-4, 0.0, 0.0));
        vec3 flatSide = normalize(cross(flatFwd, bootUp));
        bootUp = cross(flatSide, flatFwd);

        float upper = taper(p, hip, knee, 0.095, 0.078, along);
        float lower = taper(p, knee, foot + bootUp * 0.07, 0.075, 0.06, along);
        float leg = smin(upper, lower, 0.02);
        if (leg < d) {
            d = leg;
            material = kBottom;
        }
        vec3 b = p - (foot + bootUp * 0.055 + flatFwd * 0.035);
        vec3 local = vec3(dot(b, flatSide), dot(b, bootUp), dot(b, flatFwd));
        boots = min(boots, ellipsoid(local, vec3(0.105, 0.085, 0.15)));
    }
    return vec3(d, material, boots);
}

vec3 hatFrame(vec3 p) {
    vec3 up = normalize(uHeadUp);
    vec3 fwd = uHeadForward - up * dot(uHeadForward, up);
    fwd = dot(fwd, fwd) > 1e-6 ? normalize(fwd) : normalize(cross(up, vec3(1.0, 0.0, 0.0)) + vec3(1e-4));
    vec3 side = cross(fwd, up);
    vec3 q = p - (uHead + up * (uHeadRadius * 0.6));
    return vec3(dot(q, side), dot(q, up), dot(q, fwd));
}

vec2 hat(vec3 p) {
    vec3 l = hatFrame(p);
    float curl = 0.075 * smoothstep(0.1, 0.34, abs(l.x)) - 0.025 * smoothstep(0.15, 0.36, abs(l.z));
    float r = length(l.xz / vec2(1.0, 1.15));
    float brim = max(r - 0.34, abs(l.y - curl) - 0.012) * 0.8;
    vec3 c = l - vec3(0.0, 0.1, 0.0);
    float crown = max(ellipsoid(c, vec3(0.16, 0.15, 0.19)), -l.y + 0.0);
    float crease = ellipsoid(l - vec3(0.0, 0.27, 0.0), vec3(0.035, 0.07, 0.15));
    crown = max(crown, -crease);
    float pinch = 0.0;
    for (int s = -1; s <= 1; s += 2) {
        pinch = min(pinch, ellipsoid(l - vec3(0.15 * float(s), 0.2, 0.12), vec3(0.04, 0.06, 0.05)));
    }
    crown = max(crown, -pinch);
    float d = smin(brim, crown, 0.02);
    float material = (l.y > 0.0 && l.y < 0.045 && brim > crown) ? kHatBand : kHat;
    return vec2(d, material);
}

float ponchoT(vec3 q) {
    float top = uBodyRadii.y + 0.05;
    float bottom = -uBodyRadii.y * 0.55;
    return clamp((top - q.y) / (top - bottom), 0.0, 1.0);
}

vec3 ponchoSpace(vec3 p) {
    vec3 q = toBody(p);
    float around = atan(q.x, q.z) / 6.2831853 * 12.0;
    around = around < 0.0 ? around + 12.0 : around;
    int a = int(floor(around)) % 12;
    int b = (a + 1) % 12;
    vec3 hem = mix(uHem[a], uHem[b], fract(around));
    float t = ponchoT(q);
    return q - hem * (t * sqrt(t));
}

float poncho(vec3 p) {
    vec3 q = ponchoSpace(p);
    float top = uBodyRadii.y + 0.05;
    float bottom = -uBodyRadii.y * 0.55 + 0.025 * sin(q.x * 38.0) * sin(q.z * 21.0 + 1.3);
    float t = ponchoT(q);
    vec2 extent = vec2(mix(0.16, uBodyRadii.x + 0.27, sqrt(t)), mix(0.13, uBodyRadii.z + 0.13, sqrt(t)));
    float side = (length(q.xz / extent) - 1.0) * min(extent.x, extent.y);
    float lid = max(q.y - top, bottom - q.y);
    return max(side, lid) * 0.7;
}

float armDrape(vec3 p) {
    vec3 q = ponchoSpace(p);
    float bottom = -uBodyRadii.y * 0.55;
    float reach = uBodyRadii.x + 0.36;
    float d = 1e9;
    for (int a = 0; a < 2; ++a) {
        for (int i = 0; i < 4; ++i) {
            vec3 from = toBody(uArms[a * 6 + i]);
            vec3 to = toBody(uArms[a * 6 + i + 1]);
            vec3 span = to - from;
            float along = clamp(dot(q - from, span) / max(dot(span, span), 1e-6), 0.0, 1.0);
            vec3 at = from + span * along;
            float outward = length(at.xz) / reach;
            vec3 rel = q - at;
            float hang = mix(0.32, 0.08, clamp(outward, 0.0, 1.0));
            float dy = rel.y > 0.0 ? rel.y : max(-rel.y - hang, 0.0);
            float sheet = length(vec2(length(rel.xz), dy)) - 0.09;
            sheet = max(sheet, (outward - 1.0) * reach);
            sheet = max(sheet, bottom - q.y);
            d = min(d, sheet);
        }
    }
    return d * 0.8;
}

vec3 ponchoColor(vec3 p) {
    vec3 q = ponchoSpace(p);
    float t = ponchoT(q);
    float zig = abs(fract(q.x * 7.0 + q.z * 3.0) - 0.5) * 0.06;
    float band = fract((t + zig) * 3.2);
    if (band < 0.07) return kPonchoCream;
    if (band < 0.11) return kPonchoBrown;
    if (band > 0.48 && band < 0.55) return kPonchoTeal;
    if (band > 0.55 && band < 0.59) return kPonchoCream;
    return kPonchoRed;
}

vec2 field(vec3 p) {
    vec2 body = torso(p);
    float along;
    float head = min(length(p - uHead) - uHeadRadius, taper(p, uNeck, uHead, 0.075, 0.07, along));
    vec2 limbs = arms(p);
    float limb = limbs.x;
    vec3 leg = legs(p);
    float d = smin(body.x, leg.x, 0.06);
    d = smin(d, limb, 0.025);
    d = smin(d, head, 0.02);
    d = smin(d, leg.z, 0.015);

    float material = body.y > 0.5 ? kBottom : kTop;
    float nearest = body.x;
    if (limb < nearest) { nearest = limb; material = limbs.y <= limb + 0.004 ? kSkin : kSleeve; }
    if (head < nearest) { nearest = head; material = kSkin; }
    if (leg.x < nearest) { nearest = leg.x; material = leg.y; }
    if (leg.z < nearest) { nearest = leg.z; material = kBoots; }
    if (uCowboy > 0.5) {
        vec2 h = hat(p);
        float wrap = smin(poncho(p), armDrape(p), 0.09);
        d = min(d, h.x);
        d = smin(d, wrap, 0.01);
        if (h.x < nearest) { nearest = h.x; material = h.y; }
        if (wrap < nearest) { nearest = wrap; material = kPoncho; }
    }
    return vec2(d, material);
}

vec3 normalAt(vec3 p) {
    const vec2 e = vec2(0.0015, 0.0);
    return normalize(vec3(field(p + e.xyy).x - field(p - e.xyy).x, field(p + e.yxy).x - field(p - e.yxy).x,
                          field(p + e.yyx).x - field(p - e.yyx).x) + vec3(1e-6));
}

vec4 face(vec3 p) {
    vec3 f = normalize(uHeadForward);
    vec3 side = normalize(cross(f, uHeadUp) + vec3(1e-5, 0.0, 0.0));
    vec3 up = cross(side, f);
    vec3 d = normalize(p - uHead);
    if (dot(d, f) < 0.15) return vec4(0.0);
    vec2 at = vec2(dot(d, side), dot(d, up));
    vec4 paint = vec4(0.0);
    for (int c = -1; c <= 1; c += 2) {
        float cheek = length((at - vec2(0.44 * float(c), -0.3)) / vec2(0.12, 0.08));
        paint = mix(paint, vec4(1.0, 0.5, 0.3, 0.25), 1.0 - smoothstep(0.7, 1.0, cheek));
    }
    for (int e = -1; e <= 1; e += 2) {
        vec2 eye = at - vec2(0.33 * float(e), 0.02);
        float open = mix(1.0, 0.12, uBlink);
        float white = length(eye / vec2(0.2, 0.2 * open));
        float pupil = length((eye - vec2(0.0, -0.03)) / vec2(0.12, 0.13 * open));
        float inside = 1.0 - smoothstep(0.92, 1.0, white);
        paint = mix(paint, vec4(0.97, 0.94, 0.86, 1.0), inside);
        paint = mix(paint, vec4(0.18, 0.1, 0.3, 1.0), 1.0 - smoothstep(0.88, 1.0, pupil));

        float lid = eye.y - (0.07 - 0.04 * eye.x * float(e));
        paint = mix(paint, vec4(uSkin * 0.85, 1.0), inside * smoothstep(-0.005, 0.005, lid));
        paint = mix(paint, vec4(0.2, 0.1, 0.08, 1.0), inside * (1.0 - smoothstep(0.008, 0.018, abs(lid))));
    }

    vec2 m = at - vec2(0.0, -0.3);
    float line = abs(m.y + 0.6 * m.x * m.x);
    float mouth = (1.0 - smoothstep(0.012, 0.024, line)) * (1.0 - smoothstep(0.06, 0.075, abs(m.x)));
    paint = mix(paint, vec4(0.25, 0.1, 0.06, 1.0), mouth);
    return paint;
}

vec3 albedoAt(vec3 p, float material) {
    if (material == kBoots) return kBoot;
    if (material == kHat) return kHatFelt;
    if (material == kHatBand) return kHatRibbon;
    if (material == kPoncho) return ponchoColor(p);
    if (material == kBottom) {
        vec3 q = toBody(p);
        float waist = -uBodyRadii.y + 0.24;
        bool open = q.z > 0.0 && abs(q.x) < 0.085;
        return (!open && q.y > waist - 0.05 && length(p - uBody) < uBodyRadii.x + uBodyRadii.y + 0.2) ? kJacketEdge : kJeans;
    }
    if (material == kSleeve) return kJacket;
    if (material == kTop) {
        vec3 q = toBody(p);

        if (q.z > 0.0) {
            float across = abs(q.x);
            if (across < 0.085) return kTee;
            if (across < 0.105) return kJacketEdge;
        }

        if (q.y > uBodyRadii.y - 0.06 && abs(q.x) > 0.06) return kJacketEdge;
        return kJacket;
    }
    vec3 skin = uSkin;
    if (length(p - uHead) < uHeadRadius + 0.012) {
        vec4 paint = face(p);
        skin = mix(skin, paint.rgb, paint.a);
    }
    return skin;
}

void main() {
    vec3 rd = normalize(vWorld - uCameraPos);
    vec3 safe = mix(rd, vec3(1e-5), lessThan(abs(rd), vec3(1e-5)));
    vec3 t0 = (uBoxCenter - uBoxHalf - uCameraPos) / safe;
    vec3 t1 = (uBoxCenter + uBoxHalf - uCameraPos) / safe;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float tNear = max(max(max(tmin.x, tmin.y), tmin.z), 0.0);
    float tFar = min(min(tmax.x, tmax.y), tmax.z);

    float t = tNear;
    vec2 hit = vec2(1e9, 0.0);
    bool found = false;
    float closest = 1e9;
    float closestT = tNear;
    for (int i = 0; i < kSteps && t < tFar; ++i) {
        hit = field(uCameraPos + rd * t);
        if (hit.x < closest) {
            closest = hit.x;
            closestT = t;
        }
        if (hit.x < 0.0012 * (1.0 + t * 0.05)) {
            found = true;
            break;
        }
        t += hit.x * 0.85;
    }

    vec3 color;
    vec3 p;
    if (!found) {
        float width = max(0.014, closestT * 0.0035);
        if (closest > width) discard;
        p = uCameraPos + rd * closestT;
        color = kInk;
    } else {
        p = uCameraPos + rd * t;
        vec3 n = normalAt(p);
        vec3 v = -rd;
        vec3 albedo = albedoAt(p, hit.y);

        vec3 l = normalize(uLightDir);
        float lit = smoothstep(-0.2, 0.45, dot(n, l));
        vec3 sky = mix(vec3(0.08, 0.07, 0.07), vec3(0.42, 0.46, 0.55), n.y * 0.5 + 0.5);
        float rim = smoothstep(0.6, 0.95, 1.0 - clamp(dot(n, v), 0.0, 1.0));
        color = albedo * (mix(vec3(0.09, 0.08, 0.11), uSunColor * 0.2, lit) + sky * 0.42) + vec3(0.6, 0.65, 0.75) * rim * 0.06;
    }

    if (uDissolve > 0.0) {
        float n = n_noise3(p * 13.0 + vec3(uSeed, -uTime * 0.7, uSeed * 0.3));
        float high = clamp((p.y - (uBoxCenter.y - uBoxHalf.y)) / max(2.0 * uBoxHalf.y, 1e-3), 0.0, 1.0);
        float left = n - (uDissolve * 1.4 - high * 0.4);
        if (left < 0.0) discard;
        color = mix(vec3(0.4, 0.72, 0.78) * 1.3, color, smoothstep(0.0, 0.09, left));
    }

    vec4 clip = uViewProj * vec4(p, 1.0);
    float ndc = clamp(clip.z / max(clip.w, 1e-6), -1.0, 1.0);
    gl_FragDepth = uSplit + (ndc * 0.5 + 0.5) * (1.0 - uSplit);
    fragColor = vec4(max(color, vec3(0.0)), 1.0);
}
