#ifndef ANOM_FRAME_GLSL
#define ANOM_FRAME_GLSL

layout(std140, binding = 0) uniform CameraBlock {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_viewport;
    vec4 u_sun_dir;
    vec4 u_sun_color_ambient;
    vec4 u_fog_color_density;
    vec4 u_spot_pos_cone[2];
    vec4 u_spot_dir_intensity[2];
    mat4 u_shadow_mat;
    vec4 u_shadow_params;
    vec4 u_point_pos_radius[4];
    vec4 u_point_color[4];
    vec4 u_sky_ambient;
    vec4 u_ground_ambient;
    mat4 u_inv_view_proj;
    vec4 u_ambient_horizon;
    vec4 u_exposure_params;
    vec4 u_retro_params;
    vec4 u_cloud_sun_color;
    vec4 u_weather;
};

#define u_retro_near (u_retro_params.x)
#define u_retro_far (u_retro_params.y)
#define u_dither_strength (u_retro_params.z)
#define u_time_seconds (u_retro_params.w)

#define u_exposure (u_exposure_params.x)
#define u_time_of_day (u_exposure_params.y)
#define u_quantise_bits (u_exposure_params.z)
#define u_pixel_scale (u_exposure_params.w)

#define u_snow_cover (u_weather.x)
#define u_snow_fall (u_weather.y)
#define u_wind (u_weather.z)

vec3 sun_toward() { return -u_sun_dir.xyz; }

vec3 ambient_for_normal(vec3 n)
{
    float up = n.y * 0.5 + 0.5;
    vec3 sky = mix(u_ambient_horizon.rgb, u_sky_ambient.rgb, smoothstep(0.5, 1.0, up));
    return mix(u_ground_ambient.rgb, sky, smoothstep(0.0, 0.5, up));
}

vec3 world_from_depth(vec2 uv, float depth)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 world = u_inv_view_proj * clip;
    return world.xyz / world.w;
}

vec3 view_ray_from_uv(vec2 uv)
{
    vec4 clip = vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    vec4 world = u_inv_view_proj * clip;
    return normalize(world.xyz / world.w - u_cam_pos.xyz);
}

#endif
