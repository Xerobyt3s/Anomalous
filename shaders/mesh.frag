#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;

layout(binding = 0) uniform sampler2D u_albedo;

out vec4 o_color;

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
