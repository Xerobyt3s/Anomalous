#version 460 core

in vec2 v_uv;

layout(location = 1) uniform float u_time;
layout(location = 2) uniform vec4 u_shield;
layout(location = 3) uniform float u_bloom;

layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_depth;
layout(binding = 3) uniform sampler2D u_bloom_tex;

out vec4 o_color;

const float EXPOSURE = 0.90;
const float SHADOW_LIFT = 1.06;
const float SATURATION = 0.94;
const float VIGNETTE = 0.55;
const float GRAIN = 0.045;
const float BLOOM_MIX = 0.035;
const vec3 SPLIT_SHADOW = vec3(-0.018, 0.006, 0.016);
const vec3 SPLIT_HIGH = vec3(0.016, 0.008, -0.012);

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 fetch(vec2 uv)
{
    vec3 c = texture(u_color, uv).rgb;
    c = mix(c, texture(u_bloom_tex, uv).rgb, BLOOM_MIX * u_bloom);
    c *= EXPOSURE;
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    return pow(max(c, vec3(0.0)), vec3(SHADOW_LIFT));
}

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

void main()
{
    gl_FragDepth = texture(u_depth, v_uv).r;

    vec2 texel = 1.0 / vec2(textureSize(u_color, 0));

    vec3 rgbM = fetch(v_uv);
    vec3 rgbNW = fetch(v_uv + texel * vec2(-1.0, -1.0));
    vec3 rgbNE = fetch(v_uv + texel * vec2(1.0, -1.0));
    vec3 rgbSW = fetch(v_uv + texel * vec2(-1.0, 1.0));
    vec3 rgbSE = fetch(v_uv + texel * vec2(1.0, 1.0));
    float lM = luma(rgbM);
    float lNW = luma(rgbNW);
    float lNE = luma(rgbNE);
    float lSW = luma(rgbSW);
    float lSE = luma(rgbSE);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    vec3 c;
    if (lMax - lMin < max(0.04, lMax * 0.125)) {
        c = rgbM;
    } else {
        vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
        float dir_reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
        float rcp_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
        dir = clamp(dir * rcp_min, vec2(-8.0), vec2(8.0)) * texel;
        vec3 rgb_a = 0.5 * (fetch(v_uv + dir * (1.0 / 3.0 - 0.5))
                          + fetch(v_uv + dir * (2.0 / 3.0 - 0.5)));
        vec3 rgb_b = rgb_a * 0.5 + 0.25 * (fetch(v_uv - dir * 0.5)
                                         + fetch(v_uv + dir * 0.5));
        float lB = luma(rgb_b);
        c = (lB < lMin || lB > lMax) ? rgb_a : rgb_b;
    }

    float lm = luma(c);
    c = mix(vec3(lm), c, SATURATION);
    c += SPLIT_SHADOW * (1.0 - smoothstep(0.0, 0.45, lm));
    c += SPLIT_HIGH * smoothstep(0.45, 1.0, lm);

    vec2 vc = v_uv - 0.5;
    c *= 1.0 - VIGNETTE * dot(vc, vc);

    float grain = hash12(gl_FragCoord.xy + fract(u_time * 7.31) * 217.0) - 0.5;
    c += grain * GRAIN * (1.0 - lm * 0.6);

    o_color = vec4(c, 1.0);
}
