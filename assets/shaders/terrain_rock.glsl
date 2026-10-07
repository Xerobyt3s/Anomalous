#ifndef ANOM_TERRAIN_ROCK_GLSL
#define ANOM_TERRAIN_ROCK_GLSL

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float terrain_rockiness(vec2 wp, float up, float height)
{
    float slope_noise = (vnoise(wp * 0.06) - 0.5) * 0.22;
    float rockiness = 1.0 - smoothstep(0.58 + slope_noise, 0.74 + slope_noise, up);
    return clamp(rockiness + smoothstep(24.0, 34.0, height) * 0.55, 0.0, 1.0);
}

#endif
