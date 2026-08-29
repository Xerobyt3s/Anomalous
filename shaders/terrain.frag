#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"

in vec3 v_world;
in vec3 v_normal;

layout(location = 0) uniform vec4 u_terrain;

layout(binding = 0) uniform sampler2D u_grass;
layout(binding = 1) uniform sampler2D u_rock;
layout(binding = 2) uniform sampler2D u_road;
layout(binding = 3) uniform sampler2D u_roadmask;

out vec4 o_color;

const vec3 ALBEDO_GRASS = vec3(0.068, 0.104, 0.036);
const vec3 ALBEDO_DRY = vec3(0.150, 0.125, 0.060);
const vec3 ALBEDO_ROCK = vec3(0.088, 0.082, 0.074);
const vec3 ALBEDO_ROAD = vec3(0.052, 0.052, 0.056);

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

vec3 spot_light(vec3 world, vec3 n, vec3 albedo, vec4 pos_cone, vec4 dir_intensity)
{
    if (dir_intensity.w <= 0.0) {
        return vec3(0.0);
    }
    vec3 to_frag = world - pos_cone.xyz;
    float dist = max(length(to_frag), 1e-4);
    vec3 l = to_frag / dist;
    float cone = smoothstep(pos_cone.w, pos_cone.w + 0.10, dot(l, dir_intensity.xyz));
    float atten = dir_intensity.w / (1.0 + 0.022 * dist * dist);
    float ndl = max(dot(n, -l), 0.0);
    return albedo * vec3(1.0, 0.93, 0.74) * (cone * atten * ndl);
}

vec3 point_light(vec3 world, vec3 n, vec3 albedo, vec4 pos_radius, vec4 color)
{
    if (pos_radius.w <= 0.0) {
        return vec3(0.0);
    }
    vec3 to_frag = world - pos_radius.xyz;
    float dist = max(length(to_frag), 1e-4);
    float x = clamp(1.0 - dist / pos_radius.w, 0.0, 1.0);
    float wrap = mix(0.35, 1.0, max(dot(n, -to_frag / dist), 0.0));
    return albedo * color.rgb * (x * x * wrap);
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

    vec3 grass_a = texture(u_grass, wp * 0.16).rgb;
    vec3 grass_b = texture(u_grass, wp * 0.043).rgb;
    float tile_break = smoothstep(0.30, 0.70, vnoise(wp * 0.021));
    vec3 grass = mix(grass_a, grass_b, tile_break * 0.65);
    float dry = smoothstep(0.38, 0.80, vnoise(wp * 0.009 + 13.7));
    grass = mix(grass, grass * vec3(1.10, 1.02, 0.68), dry * 0.55);

    vec3 rock = texture(u_rock, wp * 0.11).rgb;
    vec3 rock_b = texture(u_rock, wp * 0.031).rgb;
    rock = mix(rock, rock_b, tile_break * 0.5);
    vec3 road = texture(u_road, wp * 0.28).rgb;

    grass = mix(ALBEDO_GRASS, ALBEDO_DRY, dry * 0.35) * (0.70 + 0.85 * luminance(grass));
    rock = ALBEDO_ROCK * (0.70 + 0.85 * luminance(rock));
    road = ALBEDO_ROAD * (0.70 + 0.85 * luminance(road));

    float slope_noise = (vnoise(wp * 0.06) - 0.5) * 0.22;
    float rockiness = 1.0 - smoothstep(0.6 + slope_noise, 0.85 + slope_noise, n.y);
    rockiness = clamp(rockiness + smoothstep(24.0, 34.0, v_world.y) * 0.55, 0.0, 1.0);
    vec3 albedo = mix(grass, rock, rockiness);

    vec2 mask_uv = (wp - u_terrain.xy) * u_terrain.zw;
    float road_amount = texture(u_roadmask, mask_uv).r;
    albedo = mix(albedo, road, road_amount);

    float macro = vnoise(wp * 0.014);
    albedo *= mix(0.84 + 0.32 * macro, 1.0, road_amount * 0.7);

    float wetness = u_shadow_params.z;
    albedo *= 1.0 - wetness * (0.28 + road_amount * 0.22);

    float ndl = max(dot(n, -u_sun_dir.xyz), 0.0);
    float shadow = shadow_factor(v_world, ndl);
    vec3 hemi = ambient_for_normal(n);
    vec3 lit = albedo * (INV_PI * u_sun_color_ambient.rgb * ndl * shadow + hemi);
    if (wetness > 0.01) {
        vec3 view = normalize(u_cam_pos.xyz - v_world);
        vec3 rdir = reflect(-view, n);
        float wet_spec = pow(max(dot(rdir, -u_sun_dir.xyz), 0.0), 90.0);
        float fresnel = pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 4.0);
        lit += INV_PI * u_sun_color_ambient.rgb * wet_spec * wetness * (0.35 + road_amount * 0.9);
        lit += u_fog_color_density.rgb * fresnel * wetness * road_amount * 0.5;
    }
    lit += spot_light(v_world, n, albedo, u_spot_pos_cone[0], u_spot_dir_intensity[0]);
    lit += spot_light(v_world, n, albedo, u_spot_pos_cone[1], u_spot_dir_intensity[1]);
    for (int i = 0; i < 4; i++) {
        lit += point_light(v_world, n, albedo, u_point_pos_radius[i], u_point_color[i]);
    }
    o_color = vec4(lit, 1.0);
}
