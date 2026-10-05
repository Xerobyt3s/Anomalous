#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"
#include "lighting.glsl"
#include "clouds.glsl"

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;
in float v_leaf;

out vec4 o_color;

const vec3 kBark = vec3(0.055, 0.044, 0.032);
const vec3 kLeaf = vec3(0.052, 0.081, 0.031);

void main()
{
    if (v_leaf > 0.5) {
        float mask = fbm2(v_uv * 9.0 + v_world.xz * 2.3, 3) + 0.5;
        float fine = fbm2(v_uv * 26.0 + v_world.xz * 5.0, 2) + 0.5;
        float radial = 1.0 - length(v_uv - 0.5) * 2.0;
        if (mask * 0.85 + fine * 0.35 + radial * 0.45 < 0.80) {
            discard;
        }
    }

    vec3 n = normalize(v_normal);
    vec3 to_camera = u_cam_pos.xyz - v_world;
    vec3 view = to_camera / max(length(to_camera), 1e-4);
    vec3 to_sun = -u_sun_dir.xyz;

    vec3 albedo = v_leaf > 0.5 ? kLeaf : kBark;
    float ndl = dot(n, to_sun);
    float shadow = shadow_factor(v_world, max(ndl, 0.0)) * cloudShadow(v_world);
    float wrapped = max((ndl + 0.2) / 1.2, 0.0);

    vec3 lit = albedo * INV_PI * u_sun_color_ambient.rgb * wrapped * shadow;
    lit += albedo * environment(n) + albedo * INV_PI * pointRadiance(v_world);
    if (v_leaf > 0.5) {
        float backlit = max(dot(-view, to_sun), 0.0);
        lit += u_sun_color_ambient.rgb * pow(backlit, 4.0) * 0.13 * shadow;
    }
    o_color = vec4(lit, 1.0);
}
