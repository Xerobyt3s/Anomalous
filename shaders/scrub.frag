#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"

in vec3 v_world;
in vec3 v_terrain_normal;
in vec3 v_blade_normal;
in float v_along;
in float v_jitter;
in float v_dry;
in float v_clump;

out vec4 o_color;

const vec3 BASE_COLOR = vec3(0.030, 0.075, 0.020);
const vec3 TIP_WARM = vec3(0.100, 0.150, 0.035);
const vec3 TIP_COOL = vec3(0.060, 0.135, 0.050);
const vec3 DRY_COLOR = vec3(0.170, 0.135, 0.055);
const vec3 SNOW_COLOR = vec3(0.78, 0.80, 0.84);
const float VALUE_JITTER = 0.40;
const float BLADE_NORMAL_WEIGHT = 0.45;
const float WRAP = 0.25;
const vec3 TRANSMISSION_COLOR = vec3(0.30, 0.60, 0.16);
const float TRANSLUCENCY = 0.45;
const float ROOT_OCCLUSION = 0.35;
const float SHEEN = 0.020;
const float AMBIENT_BOOST = 1.8;

void main()
{
    float t = v_along;
    vec3 tip = mix(TIP_WARM, TIP_COOL, v_clump);
    vec3 albedo = mix(BASE_COLOR, tip, pow(t, 1.2));
    albedo *= 1.0 + VALUE_JITTER * (v_jitter - 0.5);
    albedo = mix(albedo, DRY_COLOR * (0.8 + 0.4 * v_jitter), v_dry);

    float wetness = u_shadow_params.z;
    albedo *= 1.0 - 0.30 * wetness;
    float frost = u_snow_cover * smoothstep(0.2, 0.9, t + 0.35 * v_jitter);
    albedo = mix(albedo, SNOW_COLOR, frost);

    vec3 to_camera = u_cam_pos.xyz - v_world;
    float distance_to_camera = length(to_camera);
    vec3 view_direction = to_camera / max(distance_to_camera, 1e-4);

    vec3 blade_normal = normalize(v_blade_normal);
    if (dot(blade_normal, view_direction) < 0.0) {
        blade_normal = -blade_normal;
    }
    vec3 normal = normalize(mix(v_terrain_normal, blade_normal, BLADE_NORMAL_WEIGHT));

    vec3 sun = sun_toward();
    float shadow = shadow_factor(v_world, max(dot(v_terrain_normal, sun), 0.0));

    float wrapped = max((dot(normal, sun) + WRAP) / (1.0 + WRAP), 0.0);
    vec3 direct = albedo * INV_PI * u_sun_color_ambient.rgb * wrapped * shadow;

    float towards_sun = max(dot(-view_direction, sun), 0.0);
    float thinness = mix(0.15, 1.0, t);
    vec3 transmitted = u_sun_color_ambient.rgb * INV_PI * TRANSMISSION_COLOR
                     * pow(towards_sun, 4.0) * thinness * TRANSLUCENCY * shadow;
    transmitted *= 1.0 - abs(dot(blade_normal, sun)) * 0.55;
    transmitted *= 1.0 - frost;

    vec3 ambient = albedo * ambient_for_normal(normal) * AMBIENT_BOOST;

    vec3 half_vector = normalize(view_direction + sun);
    float sheen = pow(max(dot(blade_normal, half_vector), 0.0), 24.0) * SHEEN;
    vec3 specular = u_sun_color_ambient.rgb * sheen * shadow * max(dot(normal, sun), 0.0);

    float root_occlusion = mix(mix(ROOT_OCCLUSION, 1.0, smoothstep(0.0, 0.55, t)), 1.0, u_snow_cover);

    vec3 color = (direct + ambient) * root_occlusion + transmitted + specular;
    o_color = vec4(color, 1.0);
}
