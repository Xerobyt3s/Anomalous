#version 460 core

in vec2 v_ndc;

layout(location = 1) uniform vec4 u_cam_fwd;
layout(location = 2) uniform vec4 u_cam_right;
layout(location = 3) uniform vec4 u_cam_up;

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
};

out vec4 o_color;

float hash13(vec3 p)
{
    p = fract(p * 443.8975);
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

void main()
{
    vec3 ray = normalize(u_cam_fwd.xyz + v_ndc.x * u_cam_right.xyz + v_ndc.y * u_cam_up.xyz);
    vec3 to_sun = -u_sun_dir.xyz;

    float day = smoothstep(0.10, 0.33, u_sun_color_ambient.w);
    float dusk = clamp(1.0 - abs(to_sun.y) * 4.0, 0.0, 1.0) * day;

    vec3 horizon = u_fog_color_density.rgb;
    vec3 zenith = mix(vec3(0.020, 0.030, 0.058), vec3(0.24, 0.42, 0.66), day);

    float h = clamp(ray.y, 0.0, 1.0);
    vec3 sky = mix(horizon, zenith, pow(h, 0.55));
    if (ray.y < 0.0) {
        sky = horizon * (1.0 + ray.y * 0.35);
    }

    float overcast = u_shadow_params.w;
    float sky_luma = dot(sky, vec3(0.30, 0.55, 0.15));
    vec3 cloud = mix(vec3(sky_luma), sky, 0.35) * mix(1.0, 0.55, overcast * day);
    sky = mix(sky, cloud, overcast);

    float s = dot(ray, to_sun);
    float clear_sky = 1.0 - overcast;
    float glow = pow(max(s, 0.0), 24.0) * (0.16 + 0.55 * dusk) * clear_sky;
    float disc = smoothstep(0.99940, 0.99975, s) * step(-0.03, to_sun.y)
               * clear_sky * clear_sky;
    vec3 sun_c = u_sun_color_ambient.rgb;
    sky += sun_c * glow;
    sky += sun_c * disc * (3.0 + 9.0 * (1.0 - day));

    float night = 1.0 - day;
    if (night > 0.01 && ray.y > 0.0) {
        vec3 cell = floor(ray * 340.0);
        float star = hash13(cell);
        float bright = smoothstep(0.9976, 0.9995, star);
        float twinkle = 0.72 + 0.28 * sin(u_cam_fwd.w * 2.3 + star * 41.0);
        sky += vec3(0.85, 0.9, 1.0) * bright * twinkle * night
             * clamp(ray.y * 3.0, 0.0, 1.0) * (1.0 - overcast);
    }

    o_color = vec4(sky, 1.0);
}
