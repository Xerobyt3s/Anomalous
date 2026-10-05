#version 460 core

#include "noise.glsl"

in vec3 vWorld;
out vec4 fragColor;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform float uSplit;
uniform vec3 uCenter;
uniform float uRadius;
uniform int uKind;
uniform float uTime;
uniform float uSeed;
uniform vec3 uTarget;
uniform float uDetach[5];
uniform float uStream;
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;

float gHit = -1.0;

const float PI = 3.14159265;

vec3 spherePoint(vec2 h) {
    float z = h.x * 2.0 - 1.0;
    float a = h.y * 2.0 * PI;
    float r = sqrt(max(0.0, 1.0 - z * z));
    return vec3(r * cos(a), z, r * sin(a));
}

float arcDistance(vec3 p, int i, float step) {
    vec2 h = vec2(float(i) * 17.3 + uSeed, step);
    vec3 a = spherePoint(vec2(n_hash(h), n_hash(h + 3.1))) * 0.88;
    vec3 b = spherePoint(vec2(n_hash(h + 7.7), n_hash(h + 11.9))) * 0.88;
    vec3 ab = b - a;
    float t = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-5), 0.0, 1.0);

    vec3 side = normalize(cross(ab, vec3(0.3, 1.0, 0.2)) + 1e-4);
    vec3 side2 = normalize(cross(ab, side) + 1e-4);
    float j = sin(t * PI) * 0.22;
    vec3 onArc = a + ab * t + (side * (n_fbm(vec2(t * 7.0, h.x + step)) - 0.5) + side2 * (n_fbm(vec2(t * 7.0 + 5.0, h.x - step)) - 0.5)) * j * 2.0;
    return length(p - onArc);
}

vec4 stormOrb(vec3 ro, vec3 rd, float tIn, float tOut) {
    float step = floor(uTime * 16.0);
    float flicker = 0.6 + 0.4 * n_hash(vec2(step, uSeed + 2.0));
    vec3 glow = vec3(0.0);
    const int kSteps = 28;
    float dt = (tOut - tIn) / float(kSteps);
    for (int s = 0; s < kSteps; ++s) {
        vec3 p = (ro + rd * (tIn + dt * (float(s) + 0.5)) - uCenter) / uRadius;
        float d = 1e9;
        for (int i = 0; i < 4; ++i) {
            d = min(d, arcDistance(p, i, step));
        }

        glow += (vec3(1.0, 0.98, 1.0) * exp(-d * 60.0) * 1.6 + vec3(0.55, 0.6, 1.0) * exp(-d * 14.0) * 0.35) * flicker;
        glow += vec3(0.5, 0.55, 1.0) * exp(-dot(p, p) * 6.0) * 0.06;
    }
    glow *= dt / uRadius * 3.0;

    vec3 hit = (ro + rd * tIn - uCenter) / uRadius;
    vec3 n = normalize(hit);
    float fres = pow(1.0 - max(dot(n, -rd), 0.0), 3.0);
    vec3 glass = vec3(0.55, 0.62, 0.8) * fres * 0.9 + vec3(1.0) * pow(max(dot(reflect(rd, n), normalize(vec3(0.4, 1.0, 0.3))), 0.0), 40.0) * 0.8;
    float alpha = clamp(0.12 + fres * 0.6, 0.0, 1.0);
    return vec4(glass + glow, alpha);
}

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

vec3 orbit(int i) {
    float fi = float(i);
    float speed = 0.7 + 0.35 * fi;
    float a = uTime * speed * (i % 2 == 0 ? 1.0 : -1.0) + fi * 2.1 + uSeed;

    vec3 tilt = normalize(vec3(sin(fi * 1.7), 1.0, cos(fi * 2.3)));
    vec3 u = normalize(cross(tilt, vec3(0.2, 0.0, 1.0)));
    vec3 v = cross(tilt, u);
    float r = 0.95 + 0.35 * sin(uTime * 0.9 + fi * 1.3);
    return (u * cos(a) + v * sin(a)) * r;
}

float capsule(vec3 p, vec3 a, vec3 b, float r) {
    vec3 ab = b - a;
    float h = clamp(dot(p - a, ab) / max(dot(ab, ab), 1e-6), 0.0, 1.0);
    return length(p - a - ab * h) - r;
}

float roundCone(vec3 p, vec3 a, vec3 b, float ra, float rb) {
    vec3 ab = b - a;
    float l2 = dot(ab, ab);
    if (l2 < 1e-6) return length(p - a) - max(ra, rb);
    float h = clamp(dot(p - a, ab) / l2, 0.0, 1.0);
    return length(p - a - ab * h) - mix(ra, rb, h);
}

float drawnOut(vec3 p, float r) {
    vec3 target = (uTarget - uCenter) / uRadius;
    float shrink = 1.0 - smoothstep(0.0, 1.0, uStream) * 0.75;
    float gone = 1.0 - smoothstep(0.8, 1.0, uStream);
    float ball = r * shrink * gone;
    vec3 tip = target * smoothstep(0.0, 0.55, uStream);
    vec3 tail = target * smoothstep(0.35, 1.0, uStream);
    float d = length(p - tail) - ball;
    if (uStream > 0.0) {
        d = smin(d, roundCone(p, tail, tip, 0.13 * gone, 0.06 * gone), 0.15);
    }
    return d;
}

vec2 bubbles(vec3 p) {
    float pulse = 1.0 + 0.08 * sin(uTime * 2.0 * PI / 1.2);
    vec3 target = (uTarget - uCenter) / uRadius;
    float main = drawnOut(p, 0.62 * pulse);
    float d = main;
    for (int i = 0; i < 5; ++i) {
        float k = clamp(uDetach[i], 0.0, 1.0);
        float ease = k * k * (3.0 - 2.0 * k);
        vec3 home = orbit(i);

        vec3 bend = normalize(cross(target - home, vec3(0.0, 1.0, 0.0)) + vec3(0.0, 0.3, 0.0) + 1e-4) * sin(k * PI) * 0.6;
        vec3 at = mix(home, target, ease) + bend;
        float size = (0.17 + 0.05 * float(i % 3)) * (1.0 - smoothstep(0.6, 1.0, k));
        vec3 back = (target - home) * (sin(k * PI) * 0.12);
        float bubble = capsule(p, at - back, at, size);

        d = smin(d, bubble, max(0.22 * (1.0 - smoothstep(0.0, 0.35, k)), 0.002));
    }

    d += (n_noise(vec2(atan(p.z, p.x) * 3.0 + uTime * 1.5, p.y * 4.0 - uTime)) - 0.5) * (0.03 + 0.05 * sin(uStream * PI));
    return vec2(d, main);
}

vec4 hazeBubble(vec3 ro, vec3 rd, float tIn, float tOut) {
    const vec3 kCore = vec3(0.3, 0.55, 1.0);
    float t = tIn;
    bool hit = false;
    for (int i = 0; i < 96 && t < tOut; ++i) {
        vec3 p = (ro + rd * t - uCenter) / uRadius;
        float d = bubbles(p).x * uRadius;
        if (d < 0.0008) {
            hit = true;
            break;
        }
        t += max(d * 0.8, 0.0008);
    }
    if (!hit) {
        return vec4(0.0, 0.0, 0.0, -1.0);
    }
    gHit = t;
    vec3 p = (ro + rd * t - uCenter) / uRadius;
    const vec2 e = vec2(0.01, 0.0);
    vec3 n = normalize(vec3(bubbles(p + e.xyy).x - bubbles(p - e.xyy).x, bubbles(p + e.yxy).x - bubbles(p - e.yxy).x,
                            bubbles(p + e.yyx).x - bubbles(p - e.yyx).x) + 1e-6);
    float facing = max(dot(n, -rd), 0.0);
    float fres = pow(1.0 - facing, 2.5);

    vec3 q = p * 2.2 + vec3(0.0, uTime * 0.35, uTime * 0.2);
    float swirl = n_fbm3(q + vec3(n_fbm3(q * 0.7 - uTime * 0.15) * 1.5));
    float depth = 1.0 - smoothstep(0.0, 0.9, length(p));
    float pulse = 0.85 + 0.15 * sin(uTime * 2.0 * PI / 1.2);
    vec3 haze = mix(vec3(0.03, 0.05, 0.2), vec3(0.16, 0.26, 0.72), smoothstep(0.3, 0.8, swirl)) * (0.9 + 0.5 * depth * pulse);

    float thick = 0.0;
    for (int s = 1; s <= 24; ++s) {
        thick += bubbles(p + rd * (0.08 * float(s))).x < 0.0 ? 0.08 : 0.0;
    }
    float edge = 1.0 - exp(-thick * 3.0);
    vec3 film = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + fres * 6.0 + uTime * 0.7);
    vec3 rim = mix(kCore, film, 0.25) * fres * 0.8 * (1.0 - 0.7 * edge);
    vec3 highlight = vec3(1.0) * pow(max(dot(reflect(rd, n), normalize(vec3(0.4, 1.0, 0.3))), 0.0), 30.0) * 0.35;
    float alpha = mix(0.12, 0.96, edge);
    return vec4(haze * alpha + rim + highlight, alpha);
}

vec4 windCloud(vec3 ro, vec3 rd, float tIn, float tOut) {
    vec3 color = vec3(0.0);
    float cover = 0.0;
    float t = tIn;
    for (int i = 0; i < 90 && t < tOut && cover < 0.97; ++i) {
        vec3 p = (ro + rd * t - uCenter) / uRadius;
        float shape = drawnOut(p, 0.55);
        if (shape > 0.35) {
            t += (shape - 0.3) * uRadius;
            continue;
        }
        vec3 q = p * 2.6 + vec3(uTime * 0.5, -uTime * 0.2, uTime * 0.3);
        float churn = n_fbm3(q + vec3(n_fbm3(q * 0.6 + uSeed) * 1.4));
        float density = smoothstep(0.12, -0.2, shape + (churn - 0.5) * 0.55);
        float stepLength = 0.06;
        if (density > 0.001) {
            if (gHit < 0.0 && density > 0.05) gHit = t;
            float a = 1.0 - exp(-density * stepLength * 9.0);
            vec3 lit = mix(vec3(0.42, 0.52, 0.62), vec3(0.85, 0.94, 1.0), clamp(0.5 + p.y * 0.8 + (churn - 0.5), 0.0, 1.0));
            color += (1.0 - cover) * a * lit * 0.55;
            cover += (1.0 - cover) * a;
        }
        t += stepLength * uRadius;
    }
    if (cover < 0.003) {
        return vec4(0.0, 0.0, 0.0, -1.0);
    }
    return vec4(color, cover);
}

uniform float uEnergy;

vec3 cells(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    float d1 = 8.0;
    float d2 = 8.0;
    float id = 0.0;
    for (int z = -1; z <= 1; ++z)
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) {
                vec3 o = vec3(x, y, z);
                vec3 h = vec3(n_hash3(i + o), n_hash3(i + o + 17.0), n_hash3(i + o + 41.0));
                vec3 r = o + h - f;
                float d = dot(r, r);
                if (d < d1) {
                    d2 = d1;
                    d1 = d;
                    id = h.x;
                } else if (d < d2) {
                    d2 = d;
                }
            }
    return vec3(sqrt(d1), sqrt(d2), id);
}

vec4 ballLightning(vec3 ro, vec3 rd) {
    float pulse = 0.5 + 0.5 * sin(uTime * 3.4 + uSeed);
    float R = uRadius * (1.0 + 0.04 * pulse);
    vec3 oc = ro - uCenter;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - R * R;
    float disc = b * b - c;
    float step = floor(uTime * 16.0);
    vec3 inner = vec3(0.95, 0.85, 1.0);

    float closest = sqrt(max(dot(oc, oc) - b * b, 0.0)) / R;
    vec3 axisU = normalize(cross(rd, vec3(0.0, 1.0, 0.0)) + 1e-4);
    vec3 axisV = cross(rd, axisU);
    vec3 nearPoint = oc - rd * b;
    float angle = atan(dot(nearPoint, axisV), dot(nearPoint, axisU));
    float spikes = pow(n_noise(vec2(angle * 7.0, step * 1.7 + uSeed)), 3.0) * 2.2 + pow(n_noise(vec2(angle * 19.0, step * 2.3)), 6.0) * 2.0;
    float out_ = max(closest - 1.0, 0.0);

    float reachOut = 1.0 - smoothstep(0.35, 0.8, out_);
    vec3 glow = vec3(0.6, 0.45, 1.0) * (exp(-out_ * 9.0) * 0.5 + exp(-out_ * 2.2 / max(spikes, 0.05)) * spikes * 0.35) * uEnergy * reachOut;
    if (disc <= 0.0 || -b + sqrt(max(disc, 0.0)) <= 0.0) {
        if (b > 0.0) return vec4(0.0, 0.0, 0.0, -1.0);
        gHit = -b;
        return vec4(glow, 0.0);
    }
    float t = max(-b - sqrt(disc), 0.0);
    gHit = t;
    vec3 n = normalize(ro + rd * t - uCenter);

    float spin = uTime * 0.25 + uSeed;
    vec3 q = vec3(n.x * cos(spin) - n.z * sin(spin), n.y, n.x * sin(spin) + n.z * cos(spin)) * 3.2;
    vec3 v = cells(q);
    float holeSize = 0.18 + 0.3 * v.z;
    float hole = 1.0 - smoothstep(holeSize - 0.06, holeSize, v.x);
    float rim = smoothstep(holeSize, holeSize + 0.05, v.x) * (1.0 - smoothstep(holeSize + 0.05, holeSize + 0.16, v.x));
    float facing = max(dot(n, -rd), 0.0);
    float fres = pow(1.0 - facing, 3.0);
    vec3 film = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + facing * 5.0 + v.z * 3.0 + uTime * 0.3);
    vec3 shell = vec3(0.05, 0.03, 0.1) + film * (0.08 + 0.35 * fres) + vec3(0.4, 0.5, 1.0) * rim * 0.25;

    vec3 light = mix(inner, film * 1.3, 0.25) * (0.9 + 0.8 * pulse) * uEnergy;
    vec3 color = mix(shell, light, hole);
    color += vec3(1.0) * pow(max(dot(reflect(rd, n), normalize(vec3(0.3, 1.0, 0.4))), 0.0), 40.0) * 0.4 * (1.0 - hole);
    return vec4(color + glow * 0.3, 1.0);
}

void main() {
    vec3 ro = uCameraPos;
    vec3 rd = normalize(vWorld - ro);
    float tIn;
    float tOut;
    if (uKind == 4) {
        tIn = 0.0;
        tOut = 0.0;
    } else if (uKind == 2 || uKind == 3) {
        vec3 inv = 1.0 / (abs(rd) + 1e-6) * sign(rd + 1e-9);
        vec3 t0 = (uBoxCenter - uBoxHalf - ro) * inv;
        vec3 t1 = (uBoxCenter + uBoxHalf - ro) * inv;
        vec3 lo = min(t0, t1);
        vec3 hi = max(t0, t1);
        tIn = max(max(max(lo.x, lo.y), lo.z), 0.0);
        tOut = min(min(hi.x, hi.y), hi.z);
        if (tOut <= tIn) discard;
    } else {
        vec3 oc = ro - uCenter;
        float b = dot(oc, rd);
        float c = dot(oc, oc) - uRadius * uRadius;
        float disc = b * b - c;
        if (disc <= 0.0) discard;
        float sq = sqrt(disc);
        tIn = max(-b - sq, 0.0);
        tOut = -b + sq;
        if (tOut <= 0.0) discard;
    }

    vec4 result = uKind == 4 ? ballLightning(ro, rd)
                : uKind == 1 ? stormOrb(ro, rd, tIn, tOut)
                : uKind == 3 ? windCloud(ro, rd, tIn, tOut)
                             : hazeBubble(ro, rd, tIn, tOut);
    if (result.a < 0.0) discard;
    vec3 color = max(result.rgb, vec3(0.0));
    if (any(isnan(color)) || any(isinf(color))) discard;

    vec4 clip = uViewProj * vec4(ro + rd * (gHit >= 0.0 ? gHit : tIn), 1.0);
    float ndc = clamp(clip.z / max(clip.w, 1e-6), -1.0, 1.0);
    gl_FragDepth = uSplit + (ndc * 0.5 + 0.5) * (1.0 - uSplit);
    fragColor = vec4(color, clamp(result.a, 0.0, 1.0));
}
