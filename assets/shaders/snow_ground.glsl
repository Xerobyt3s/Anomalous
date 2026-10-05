#ifndef ANOM_SNOW_GROUND_GLSL
#define ANOM_SNOW_GROUND_GLSL

#include "common.glsl"
#include "frame.glsl"

const float SNOW_ALBEDO = 0.78;
const float SNOW_TONE_LOW = 0.92;
const float SNOW_TONE_HIGH = 1.04;
const vec2 SNOW_WIND = vec2(0.86, 0.51);
const float SNOW_SASTRUGI_SCALE = 1.0 / 1.6;
const float SNOW_SASTRUGI_STRETCH = 4.0;
const float SNOW_SASTRUGI_SLOPE = 0.45;
const float SNOW_SASTRUGI_FADE_START = 25.0;
const float SNOW_SASTRUGI_FADE_END = 60.0;
const vec3 SNOW_SHADOW_TINT = vec3(0.90, 0.95, 1.02);

float snow_coverage_up(vec3 world, vec3 n, vec3 up, float road)
{
    if (u_snow_cover <= 0.0) {
        return 0.0;
    }
    float flat_ground = smoothstep(0.45, 0.80, dot(n, up));
    float patches = clamp(fbm2(world.xz * 0.35 + world.y * 0.27, 3) + 0.5, 0.0, 1.0);
    float cover = u_snow_cover * mix(1.0, 0.5, road);
    float need = 1.0 - cover;
    return smoothstep(need, need + 0.15, flat_ground * (0.70 + 0.30 * patches));
}

float snow_coverage(vec3 world, vec3 n, float road)
{
    return snow_coverage_up(world, n, vec3(0.0, 1.0, 0.0), road);
}

vec3 snow_albedo(vec3 world)
{
    float tone = fbm2(world.xz * 0.05, 3) + 0.5;
    return vec3(SNOW_ALBEDO) * mix(SNOW_TONE_LOW, SNOW_TONE_HIGH, tone);
}

vec3 snow_normal(vec3 world, vec3 n)
{
    float distance = length(u_cam_pos.xyz - world);
    float detail = 1.0 - smoothstep(SNOW_SASTRUGI_FADE_START, SNOW_SASTRUGI_FADE_END, distance);
    if (detail <= 0.001) {
        return n;
    }
    vec2 p = world.xz;
    vec2 along = SNOW_WIND;
    vec2 across = vec2(-along.y, along.x);
    vec2 q = vec2(dot(p, along) / SNOW_SASTRUGI_STRETCH, dot(p, across)) * SNOW_SASTRUGI_SCALE;
    float e = 0.05;
    float h0 = fbm2(q, 3);
    float hx = fbm2(q + vec2(e, 0.0), 3);
    float hy = fbm2(q + vec2(0.0, e), 3);
    vec2 grad = vec2(hx - h0, hy - h0) / e;
    vec2 slope = (along * (grad.x / SNOW_SASTRUGI_STRETCH) + across * grad.y)
               * SNOW_SASTRUGI_SCALE * SNOW_SASTRUGI_SLOPE * detail;
    return normalize(vec3(n.x - slope.x, n.y, n.z - slope.y));
}

vec3 snow_shade_tint(vec3 n)
{
    vec3 wrapped = normalize(mix(n, vec3(0.0, 1.0, 0.0), 0.5));
    return mix(SNOW_SHADOW_TINT, vec3(1.0), wrapped.y);
}

#endif
