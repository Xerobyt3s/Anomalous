#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"
#include "snow_ground.glsl"

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in vec3 v_up;
in vec3 v_obj;
in vec3 v_obj_normal;
in mat3 v_basis;

layout(location = 10) uniform int u_maps;

layout(binding = 0) uniform sampler2D u_albedo;
layout(binding = 4) uniform sampler2D u_normal_map;
layout(binding = 5) uniform sampler2D u_surface_map;
layout(binding = 6) uniform sampler2D u_blend_map;
layout(binding = 8) uniform sampler2D u_tri_grass;
layout(binding = 9) uniform sampler2D u_tri_soil;
layout(binding = 10) uniform sampler2D u_tri_soil_n;
layout(binding = 11) uniform sampler2D u_tri_rock;
layout(binding = 12) uniform sampler2D u_tri_rock_n;
layout(binding = 13) uniform sampler2D u_tri_rock_s;

const float GROUND_FAR_START = 18.0;
const float GROUND_FAR_END = 45.0;
const float GROUND_FAR_LOD = 9.0;
const float GROUND_ROUGHNESS = 0.88;
const float WET_DARKEN = 0.28;
const float WET_ROUGHNESS = 0.22;
const float SNOW_ROUGHNESS = 0.7;
const float TRI_GRASS_SCALE = 0.25;
const float TRI_SOIL_SCALE = 0.45;
const float TRI_ROCK_SCALE = 0.3;
const float TRI_ROCK_SCALE_FAR = 0.07;
const float TRI_SHARPNESS = 4.0;

vec3 tri_weights(vec3 n)
{
    vec3 w = pow(abs(n), vec3(TRI_SHARPNESS));
    return w / max(w.x + w.y + w.z, 1e-5);
}

vec3 tri_sample(sampler2D t, vec3 p, vec3 w, float scale)
{
    return texture(t, p.zy * scale).rgb * w.x + texture(t, p.xz * scale).rgb * w.y
         + texture(t, p.xy * scale).rgb * w.z;
}

vec3 tri_normal(sampler2D t, vec3 p, vec3 n, vec3 w, float scale)
{
    vec3 tx = texture(t, p.zy * scale).xyz * 2.0 - 1.0;
    vec3 ty = texture(t, p.xz * scale).xyz * 2.0 - 1.0;
    vec3 tz = texture(t, p.xy * scale).xyz * 2.0 - 1.0;
    tx = vec3(tx.xy + n.zy, abs(tx.z) * n.x);
    ty = vec3(ty.xy + n.xz, abs(ty.z) * n.y);
    tz = vec3(tz.xy + n.xy, abs(tz.z) * n.z);
    return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
}

out vec4 o_color;

mat3 cotangent_frame(vec3 n, vec3 p, vec2 uv)
{
    vec3 dp1 = dFdx(p);
    vec3 dp2 = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 dp2perp = cross(dp2, n);
    vec3 dp1perp = cross(n, dp1);
    vec3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float scale = inversesqrt(max(max(dot(t, t), dot(b, b)), 1e-20));
    return mat3(t * scale, b * scale, n);
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
    vec3 albedo = texture(u_albedo, v_uv).rgb;
    vec3 view = normalize(u_cam_pos.xyz - v_world);
    vec3 to_sun = -u_sun_dir.xyz;

    if (u_maps != 0) {
        vec3 geometric = n;
        float roughness = 0.6;
        float metal = 0.0;
        float cavity = 1.0;
        if ((u_maps & 16) != 0) {
            vec3 on = normalize(v_obj_normal);
            vec3 w = tri_weights(on);
            float dist = length(u_cam_pos.xyz - v_world);
            float breakup = fbm2(v_obj.xz * 0.35 + v_obj.y * 0.27, 3) - 0.5;
            float c = clamp(v_uv.x + breakup * 0.3, 0.0, 1.0);
            float w_grass = 1.0 - smoothstep(0.15, 0.35, c);
            float w_rock = smoothstep(0.62, 0.85, c);
            float w_soil = max(1.0 - w_grass - w_rock, 0.0);
            vec3 grass = texture(u_tri_grass, v_obj.xz * TRI_GRASS_SCALE).rgb;
            grass = mix(grass, textureLod(u_tri_grass, v_obj.xz * TRI_GRASS_SCALE, GROUND_FAR_LOD).rgb,
                        smoothstep(GROUND_FAR_START, GROUND_FAR_END, dist) * 0.8);
            vec3 soil = tri_sample(u_tri_soil, v_obj, w, TRI_SOIL_SCALE);
            vec3 rock = tri_sample(u_tri_rock, v_obj, w, TRI_ROCK_SCALE);
            vec3 rock_far = tri_sample(u_tri_rock, v_obj, w, TRI_ROCK_SCALE_FAR);
            rock *= mix(vec3(1.0), rock_far / max(luminance(rock_far), 1e-3) * 0.5 + 0.5, 0.6);
            vec3 rock_s = tri_sample(u_tri_rock_s, v_obj, w, TRI_ROCK_SCALE);
            albedo = grass * w_grass + soil * w_soil + rock * w_rock;
            vec3 n_obj = on;
            if (w_rock > 0.0) {
                n_obj = normalize(mix(n_obj, tri_normal(u_tri_rock_n, v_obj, on, w, TRI_ROCK_SCALE), w_rock));
            }
            if (w_soil > 0.0) {
                n_obj = normalize(mix(n_obj, tri_normal(u_tri_soil_n, v_obj, on, w, TRI_SOIL_SCALE), w_soil));
            }
            n = normalize(v_basis * n_obj);
            roughness = GROUND_ROUGHNESS * (w_grass + w_soil) + rock_s.r * w_rock;
            cavity = v_uv.y * mix(1.0, rock_s.b, w_rock);
        }
        if ((u_maps & 1) != 0) {
            vec3 tangent_normal = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;
            n = normalize(cotangent_frame(geometric, v_world, v_uv) * tangent_normal);
        }
        if ((u_maps & 2) != 0) {
            vec3 surface = texture(u_surface_map, v_uv).rgb;
            roughness = surface.r;
            metal = surface.g;
            cavity = surface.b;
        }
        if ((u_maps & 4) != 0) {
            float dist = length(u_cam_pos.xyz - v_world);
            vec3 far = textureLod(u_albedo, v_uv, GROUND_FAR_LOD).rgb;
            albedo = mix(albedo, far, smoothstep(GROUND_FAR_START, GROUND_FAR_END, dist) * 0.8);
            float jitter = (fbm2(v_world.xz * 0.35 + v_world.y * 0.21, 3) - 0.5) * 0.18;
            float slope = dot(geometric, normalize(v_up));
            float rockiness = 1.0 - smoothstep(0.60 + jitter, 0.78 + jitter, slope);
            albedo = mix(albedo, texture(u_blend_map, v_uv * 0.6).rgb, rockiness);
            roughness = GROUND_ROUGHNESS;
        }
        if ((u_maps & 8) != 0) {
            float wetness = u_shadow_params.z;
            float snow = snow_coverage_up(v_world, geometric, normalize(v_up), 0.0);
            if (snow > 0.0) {
                albedo = mix(albedo, snow_albedo(v_world), snow);
                roughness = mix(roughness, SNOW_ROUGHNESS, snow);
                cavity = mix(cavity, 1.0, snow);
                wetness *= 1.0 - snow;
            }
            albedo *= 1.0 - wetness * WET_DARKEN;
            roughness = mix(roughness, WET_ROUGHNESS, wetness);
        }
        float ndl = max(dot(n, to_sun), 0.0);
        float shadow = shadow_factor(v_world, max(dot(geometric, to_sun), 0.0));
        vec3 hemi = ambient_for_normal(n);
        vec3 diffuse_albedo = albedo * (1.0 - 0.5 * metal);
        vec3 lit = diffuse_albedo * (INV_PI * u_sun_color_ambient.rgb * ndl * shadow + hemi) * cavity;

        vec3 hlf = normalize(view + to_sun);
        float gloss = 1.0 - roughness;
        float power = mix(6.0, 260.0, gloss * gloss);
        float f0 = mix(0.04, 0.30, metal);
        float cosine = clamp(dot(n, view), 0.0, 1.0);
        float fresnel = f0 + (max(gloss, f0) - f0) * pow(1.0 - cosine, 5.0);
        float spec = pow(max(dot(n, hlf), 0.0), power) * (power + 8.0) / (8.0 * PI);
        vec3 spec_tint = mix(vec3(1.0), albedo / max(luminance(albedo), 1e-3), metal * 0.6);
        lit += INV_PI * u_sun_color_ambient.rgb * spec * fresnel * spec_tint
             * smoothstep(0.0, 0.12, ndl) * shadow * cavity;
        lit += ambient_for_normal(reflect(-view, n)) * fresnel * cavity * gloss;
        lit += spot_light(v_world, n, albedo, u_spot_pos_cone[0], u_spot_dir_intensity[0]);
        lit += spot_light(v_world, n, albedo, u_spot_pos_cone[1], u_spot_dir_intensity[1]);
        for (int i = 0; i < 4; i++) {
            lit += point_light(v_world, n, albedo, u_point_pos_radius[i], u_point_color[i]);
        }
        o_color = vec4(lit, 1.0);
        return;
    }

    float ndl = max(dot(n, to_sun), 0.0);
    float shadow = shadow_factor(v_world, ndl);
    vec3 hemi = ambient_for_normal(n);
    vec3 lit = albedo * (INV_PI * u_sun_color_ambient.rgb * ndl * shadow + hemi);
    vec3 hlf = normalize(view + to_sun);
    float fresnel = pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 5.0);
    float spec = pow(max(dot(n, hlf), 0.0), 48.0);
    lit += INV_PI * u_sun_color_ambient.rgb * spec * (0.20 + 0.55 * fresnel)
         * smoothstep(0.0, 0.12, ndl) * shadow;
    lit += u_sky_ambient.rgb * fresnel * 0.30;
    lit += spot_light(v_world, n, albedo, u_spot_pos_cone[0], u_spot_dir_intensity[0]);
    lit += spot_light(v_world, n, albedo, u_spot_pos_cone[1], u_spot_dir_intensity[1]);
    for (int i = 0; i < 4; i++) {
        lit += point_light(v_world, n, albedo, u_point_pos_radius[i], u_point_color[i]);
    }
    o_color = vec4(lit, 1.0);
}
