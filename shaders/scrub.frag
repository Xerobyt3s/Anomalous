#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "shadow.glsl"

in vec3 v_world;
in vec3 v_terrain_normal;
in vec3 v_blade_normal;
in float v_along;
in float v_jitter;

out vec4 o_color;

const vec3 BASE_COLOR = vec3(0.020, 0.092, 0.028);
const vec3 TIP_COLOR = vec3(0.115, 0.105, 0.045);
const vec3 TRANSMISSION_COLOR = vec3(0.32, 0.52, 0.14);
const float TRANSLUCENCY = 0.60;
const float AMBIENT_BOOST = 1.25;

void main()
{
    vec3 albedo = mix(BASE_COLOR, TIP_COLOR, v_along * v_along);
    albedo *= 0.80 + 0.40 * v_jitter;

    float wetness = u_shadow_params.z;
    albedo *= 1.0 - 0.30 * wetness;

    vec3 to_camera = u_cam_pos.xyz - v_world;
    float distance_to_camera = length(to_camera);
    vec3 view_direction = to_camera / max(distance_to_camera, 1e-4);

    vec3 sun = sun_toward();
    float sun_cosine = dot(v_terrain_normal, sun);
    float wrapped = max((sun_cosine + 0.10) / 1.10, 0.0);
    float shadow = shadow_factor(v_world, max(sun_cosine, 0.0));

    vec3 direct = albedo * INV_PI * u_sun_color_ambient.rgb * wrapped * shadow;

    float towards_sun = max(dot(-view_direction, sun), 0.0);
    float thinness = mix(0.15, 1.0, v_along);
    vec3 tint = mix(TRANSMISSION_COLOR, vec3(0.86, 0.70, 0.30), v_along * v_along);
    vec3 transmitted = u_sun_color_ambient.rgb * INV_PI * tint * pow(towards_sun, 4.0)
                     * thinness * TRANSLUCENCY * shadow;
    transmitted *= 1.0 - abs(dot(v_blade_normal, sun)) * 0.55;

    vec3 ambient = albedo * ambient_for_normal(v_terrain_normal) * AMBIENT_BOOST;

    float root_occlusion = mix(0.30, 1.0, smoothstep(0.0, 0.55, v_along));

    vec3 color = (direct + ambient) * root_occlusion + transmitted;
    o_color = vec4(color, 1.0);
}
