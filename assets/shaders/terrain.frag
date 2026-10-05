#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"
#include "snow_ground.glsl"
#include "lighting.glsl"
#include "clouds.glsl"

in vec3 v_world;
in vec3 v_normal;

layout(location = 0) uniform vec4 u_terrain;

layout(binding = 0) uniform sampler2D u_grass;
layout(binding = 1) uniform sampler2D u_rock;
layout(binding = 2) uniform sampler2D u_road;
layout(binding = 3) uniform sampler2D u_roadmask;
layout(binding = 5) uniform sampler2D u_meadow;

out vec4 o_color;

const vec3 ALBEDO_ROCK = vec3(0.088, 0.082, 0.074);
const vec3 ALBEDO_ROAD = vec3(0.052, 0.052, 0.056);
const float EDGE_FADE_METRES = 70.0;
const float TERRAIN_ROUGHNESS = 0.92;
const float WET_ROUGHNESS = 0.25;
const float TERRAIN_GRAZING = 0.35;
const vec3 MEADOW_SOIL = vec3(0.034, 0.030, 0.017);
const vec3 MEADOW_BASE = vec3(0.024, 0.052, 0.015);
const vec3 MEADOW_FAR = vec3(0.075, 0.120, 0.034);
const vec3 MEADOW_FAR_DRY = vec3(0.120, 0.125, 0.045);
const float MEADOW_FAR_START = 20.0;
const float MEADOW_FAR_END = 45.0;
const float MEADOW_TILE_METRES = 4.0;
const float MEADOW_GAIN = 0.55;
const vec3 MEADOW_GRADE = vec3(0.92, 1.12, 1.60);

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

vec3 apply_fog(vec3 lit, vec3 world)
{
    vec3 to_frag = world - u_cam_pos.xyz;
    float dist = length(to_frag);
    float height_falloff = exp(-max(world.y - 6.0, 0.0) * 0.035);
    float density = u_fog_color_density.w * mix(0.55, 1.0, height_falloff);
    float fog_amount = 1.0 - exp(-pow(dist * density, 2.0));
    vec3 vdir = to_frag / max(dist, 1e-4);
    float sun_glow = pow(max(dot(vdir, -u_sun_dir.xyz), 0.0), 9.0);
    vec3 fog_c = u_fog_color_density.rgb
               + u_sun_color_ambient.rgb * sun_glow * 0.45 * (1.0 - u_shadow_params.w);
    return mix(lit, fog_c, fog_amount);
}

void main()
{
    vec3 n = normalize(v_normal);
    vec2 wp = v_world.xz;

    float tile_break = smoothstep(0.30, 0.70, vnoise(wp * 0.021));
    float patches = fbm2(wp * 0.011, 4) + 0.5;
    float fine = fbm2(wp * 0.14, 3);
    vec3 photo = srgbToLinear(texture(u_meadow, wp / MEADOW_TILE_METRES).rgb) * MEADOW_GAIN
               * MEADOW_GRADE;
    vec3 tint = mix(MEADOW_SOIL, MEADOW_BASE, clamp(patches * 1.1 + fine * 0.5, 0.0, 1.0));
    vec3 near = photo * (0.6 + 0.4 * tint / (0.5 * (MEADOW_SOIL + MEADOW_BASE)));
    vec3 far = mix(MEADOW_FAR, MEADOW_FAR_DRY, clamp(patches * 1.3, 0.0, 1.0));
    float ground_distance = length(u_cam_pos.xyz - v_world);
    vec3 grass = mix(near, far, smoothstep(MEADOW_FAR_START, MEADOW_FAR_END, ground_distance));
    grass *= 0.85 + 0.3 * fine;

    vec3 rock = texture(u_rock, wp * 0.11).rgb;
    vec3 rock_b = texture(u_rock, wp * 0.031).rgb;
    rock = mix(rock, rock_b, tile_break * 0.5);
    vec3 road = texture(u_road, wp * 0.28).rgb;

    rock = ALBEDO_ROCK * (0.70 + 0.85 * luminance(rock));
    road = ALBEDO_ROAD * (0.70 + 0.85 * luminance(road));

    float slope_noise = (vnoise(wp * 0.06) - 0.5) * 0.22;
    float rockiness = 1.0 - smoothstep(0.58 + slope_noise, 0.74 + slope_noise, n.y);
    rockiness = clamp(rockiness + smoothstep(24.0, 34.0, v_world.y) * 0.55, 0.0, 1.0);
    vec3 albedo = mix(grass, rock, rockiness);

    vec2 mask_uv = (wp - u_terrain.xy) * u_terrain.zw;
    float road_amount = texture(u_roadmask, mask_uv).r;
    albedo = mix(albedo, road, road_amount);

    float macro = vnoise(wp * 0.014);
    albedo *= mix(0.84 + 0.32 * macro, 1.0, road_amount * 0.7);

    float wetness = u_shadow_params.z;
    float snow = snow_coverage(v_world, n, road_amount);
    if (snow > 0.0) {
        albedo = mix(albedo, snow_albedo(v_world), snow);
        n = normalize(mix(n, snow_normal(v_world, n), snow));
        wetness *= 1.0 - snow;
    }
    albedo *= 1.0 - wetness * (0.28 + road_amount * 0.22);

    vec3 view = normalize(u_cam_pos.xyz - v_world);
    vec3 sun = sun_toward();
    gBase = albedo;
    gMetal = 0.0;
    gGrazing = TERRAIN_GRAZING;
    gRough = mix(TERRAIN_ROUGHNESS, WET_ROUGHNESS, wetness * (0.45 + road_amount * 0.55));
    vec3 F0 = vec3(0.04);
    float shadow = shadow_factor(v_world, max(dot(n, sun), 0.0)) * cloudShadow(v_world);
    vec3 lit = shade(n, view, sun, u_sun_color_ambient.rgb, F0) * shadow;
    lit += shadePoints(v_world, n, view, F0);
    lit += ambientLight(n, view, F0) * mix(vec3(1.0), snow_shade_tint(n), snow);

    vec2 to_edge = min(mask_uv, 1.0 - mask_uv) / u_terrain.zw;
    float edge = smoothstep(0.0, EDGE_FADE_METRES, min(to_edge.x, to_edge.y));
    lit = mix(u_fog_color_density.rgb, lit, edge);

    o_color = vec4(lit, 1.0);
}
