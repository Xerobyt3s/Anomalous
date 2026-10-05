#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "atmosphere.glsl"

in vec2 v_uv;

layout(location = 1) uniform float u_time;
layout(location = 2) uniform vec4 u_shield;
layout(location = 3) uniform float u_bloom;
layout(location = 4) uniform vec2 u_warp;
layout(location = 5) uniform vec2 u_chroma;

layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_depth;
layout(binding = 3) uniform sampler2D u_bloom_tex;

out vec4 o_color;

const float SHADOW_LIFT = 1.06;
const float SATURATION = 0.94;
const float VIGNETTE = 0.55;
const float GRAIN = 0.045;
const float BLOOM_MIX = 0.035;
const vec3 SPLIT_SHADOW = vec3(-0.018, 0.006, 0.016);
const vec3 SPLIT_HIGH = vec3(0.016, 0.008, -0.012);

const mat3 kAgxTransform =
    mat3(0.842479062253094, 0.0423282422610123, 0.0423756549057051, 0.0784335999999992,
         0.878468636469772, 0.0784336, 0.0792237451477643, 0.0791661274605434, 0.879142973793104);

const mat3 kAgxTransformInverse =
    mat3(1.19687900512017, -0.0528968517574562, -0.0529716355144438, -0.0980208811401368,
         1.15190312990417, -0.0980434501171241, -0.0990297440797205, -0.0989611768448433,
         1.15107367264116);

vec3 agxContrast(vec3 x)
{
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return 15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x + 0.4298 * x2 +
           0.1191 * x - 0.00232;
}

vec3 tonemapAgx(vec3 color)
{
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    color = kAgxTransform * color;
    color = clamp(log2(max(color, 1e-10)), minEv, maxEv);
    color = (color - minEv) / (maxEv - minEv);
    color = agxContrast(color);

    float grey = luminance(color);
    color = grey + (color - grey) * 1.10;

    color = kAgxTransformInverse * color;
    return pow(max(color, 0.0), vec3(2.2));
}

vec3 g_fog_transmittance = vec3(1.0);
vec3 g_fog_inscatter = vec3(0.0);
float g_px_scale = 1.0;
float g_haze = 0.0;

const float HAZE_SKY_DISTANCE = 600.0;
const float HAZE_CHROMA_PIXELS = 7.0;
const float HAZE_SHIMMER_UV = 0.0025;
const float HAZE_FADE_SPAN = 0.75;
const float HAZE_INSIDE = 0.4;

float haze_optical_depth(vec3 ro, vec3 rd, float dist)
{
    float depth = 0.0;
    for (int i = 0; i < u_haze_count; i++) {
        vec3 c = u_haze[i].xyz;
        float r = u_haze[i].w;
        vec3 oc = ro - c;
        float core = max(r - u_haze_tint.w, 0.0);
        float outside = mix(HAZE_INSIDE, 1.0, smoothstep(core, core + u_haze_tint.w * HAZE_FADE_SPAN, length(oc)));
        if (outside <= 0.0) {
            continue;
        }
        float b = dot(oc, rd);
        float h = b * b - (dot(oc, oc) - r * r);
        if (h <= 0.0) {
            continue;
        }
        h = sqrt(h);
        float t0 = max(-b - h, 0.0);
        float t1 = min(-b + h, dist);
        if (t1 <= t0) {
            continue;
        }
        float closest = length(oc + rd * clamp(-b, t0, t1)) / r;
        depth += (t1 - t0) * (1.0 - closest * closest * 0.7) * outside;
    }
    return depth * u_haze_density;
}

void apply_haze(vec3 dir, float dist)
{
    if (u_haze_count <= 0) {
        return;
    }
    float depth = haze_optical_depth(u_cam_pos.xyz, dir, dist);
    if (depth <= 0.0) {
        return;
    }
    float transmittance = exp(-depth);
    float lum = dot(u_fog_color_density.rgb, vec3(0.3, 0.55, 0.15));
    vec3 lit = u_haze_tint.rgb * (lum * 1.15 + dot(u_sun_color_ambient.rgb, vec3(0.3, 0.55, 0.15)) * 0.02);
    g_fog_inscatter = g_fog_inscatter * transmittance + lit * (1.0 - transmittance);
    g_fog_transmittance *= transmittance;
    g_haze = 1.0 - transmittance;
}

vec2 chroma_spread(vec2 uv)
{
    if (u_chroma.x <= 0.0 && g_haze <= 0.0) {
        return vec2(0.0);
    }
    vec2 d = uv - 0.5;
    float r = length(d * vec2(u_viewport.x / u_viewport.y, 1.0));
    if (r < 1e-4) {
        return vec2(0.0);
    }
    float pulse = 0.75 + 0.25 * sin(u_time * 2.0 + u_chroma.y);
    float px = (u_chroma.x + u_haze_chroma * HAZE_CHROMA_PIXELS * g_haze) * pulse * smoothstep(0.05, 0.6, r)
             * (u_viewport.y / 1080.0);
    px = g_px_scale > 1.0 ? floor(px / g_px_scale + 0.5) * g_px_scale : px;
    return normalize(d) * px / u_viewport.xy;
}

vec3 fetch(vec2 uv)
{
    vec3 c;
    vec2 spread = (uv - 0.5) * (u_warp.x * 0.020) + chroma_spread(uv);
    if (dot(spread, spread) > 0.0) {
        c = vec3(texture(u_color, uv + spread).r, texture(u_color, uv).g,
                 texture(u_color, uv - spread).b);
    } else {
        c = texture(u_color, uv).rgb;
    }
    c = c * g_fog_transmittance + g_fog_inscatter;
    c = mix(c, texture(u_bloom_tex, uv).rgb, BLOOM_MIX * u_bloom);
    c = tonemapAgx(c * u_exposure);
    return pow(max(c, vec3(0.0)), vec3(SHADOW_LIFT));
}

float luma(vec3 c)
{
    return dot(c, vec3(0.299, 0.587, 0.114));
}

float view_distance(vec2 uv)
{
    float d = texture(u_depth, uv).r;
    if (d >= 1.0) {
        return 1.0e6;
    }
    return length(world_from_depth(uv, d) - u_cam_pos.xyz);
}

void main()
{
    float centre_depth = texture(u_depth, v_uv).r;
    gl_FragDepth = centre_depth;

    if (centre_depth < 1.0) {
        vec3 to_frag = world_from_depth(v_uv, centre_depth) - u_cam_pos.xyz;
        float dist = length(to_frag);
        g_fog_inscatter = aerialPerspective(worldAltitude(u_cam_pos.xyz), to_frag / max(dist, 1e-4),
                                            sun_toward(), dist * 0.001, g_fog_transmittance);
        apply_haze(to_frag / max(dist, 1e-4), dist);
    } else {
        vec3 sky_dir = normalize(world_from_depth(v_uv, 0.999) - u_cam_pos.xyz);
        apply_haze(sky_dir, HAZE_SKY_DISTANCE);
    }

    float retro = u_pixel_scale > 1.0 || u_quantise_bits > 0.0
                      ? 1.0 - smoothstep(u_retro_near, u_retro_far, view_distance(v_uv))
                      : 0.0;

    float scale = floor(mix(1.0, max(u_pixel_scale, 1.0), retro) + 0.5);
    g_px_scale = scale;
    vec2 uv = v_uv;
    if (u_warp.x > 0.0) {
        // Wring the frame in toward the centre and twist it, hardest at the corners, so
        // the throat looks like it is taking the whole view with it.
        vec2 d = uv - 0.5;
        float r = length(d);
        float turn = u_warp.x * 1.5 * (1.0 - min(r * 1.4, 1.0));
        float sn = sin(turn);
        float cs = cos(turn);
        d = vec2(d.x * cs - d.y * sn, d.x * sn + d.y * cs);
        uv = 0.5 + d * (1.0 - u_warp.x * (0.30 + 1.20 * r * r));
    }
    if (g_haze > 0.0 && u_haze_shimmer > 0.0) {
        vec2 wobble = vec2(sin(v_uv.y * 47.0 + u_time * 1.7 + sin(v_uv.x * 13.0 + u_time * 0.6) * 2.0),
                           cos(v_uv.x * 41.0 - u_time * 1.3 + sin(v_uv.y * 11.0 - u_time * 0.5) * 2.0));
        uv += wobble * (HAZE_SHIMMER_UV * u_haze_shimmer * g_haze);
    }
    if (scale > 1.0) {
        vec2 grid = max(u_viewport.xy / scale, vec2(1.0));
        uv = (floor(v_uv * grid) + 0.5) / grid;
    }

    vec2 texel = 1.0 / vec2(textureSize(u_color, 0));

    vec3 rgbM = fetch(uv);
    vec3 rgbNW = fetch(uv + texel * vec2(-1.0, -1.0));
    vec3 rgbNE = fetch(uv + texel * vec2(1.0, -1.0));
    vec3 rgbSW = fetch(uv + texel * vec2(-1.0, 1.0));
    vec3 rgbSE = fetch(uv + texel * vec2(1.0, 1.0));
    float lM = luma(rgbM);
    float lNW = luma(rgbNW);
    float lNE = luma(rgbNE);
    float lSW = luma(rgbSW);
    float lSE = luma(rgbSE);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    vec3 c;
    if (scale > 1.0 || lMax - lMin < max(0.04, lMax * 0.125)) {
        c = rgbM;
    } else {
        vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
        float dir_reduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.125, 1.0 / 128.0);
        float rcp_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
        dir = clamp(dir * rcp_min, vec2(-8.0), vec2(8.0)) * texel;
        vec3 rgb_a = 0.5 * (fetch(uv + dir * (1.0 / 3.0 - 0.5))
                          + fetch(uv + dir * (2.0 / 3.0 - 0.5)));
        vec3 rgb_b = rgb_a * 0.5 + 0.25 * (fetch(uv - dir * 0.5)
                                         + fetch(uv + dir * 0.5));
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

    if (u_quantise_bits > 0.0 && retro > 0.0) {
        float bits = mix(8.0, u_quantise_bits, retro);
        float levels = max(exp2(bits) - 1.0, 1.0);
        vec2 dither_pixel = scale > 1.0 ? floor(gl_FragCoord.xy / scale) : gl_FragCoord.xy;
        c += bayerOffset(ivec2(dither_pixel)) * u_dither_strength / levels;
        c = floor(clamp(c, 0.0, 1.0) * levels + 0.5) / levels;
    }

    c = mix(c, vec3(1.0), clamp(u_warp.y, 0.0, 1.0));

    o_color = vec4(c, 1.0);
}
