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
    vec4 u_sky_ambient;
    vec4 u_ground_ambient;
};

out vec4 o_color;

float hash13(vec3 p)
{
    p = fract(p * 443.8975);
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

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

float fbm(vec2 p)
{
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; i++) {
        v += amp * vnoise(p);
        p = p * 2.03 + vec2(17.3, 9.1);
        amp *= 0.5;
    }
    return v;
}

void main()
{
    vec3 ray = normalize(u_cam_fwd.xyz + v_ndc.x * u_cam_right.xyz + v_ndc.y * u_cam_up.xyz);
    vec3 to_sun = -u_sun_dir.xyz;
    float time = u_cam_fwd.w;

    float day = smoothstep(0.10, 0.33, u_sun_color_ambient.w);
    float dusk = clamp(1.0 - abs(to_sun.y) * 4.0, 0.0, 1.0) * day;

    vec3 horizon = u_fog_color_density.rgb;
    vec3 zenith = mix(vec3(0.016, 0.024, 0.048), vec3(0.14, 0.24, 0.37), day);

    float h = clamp(ray.y, 0.0, 1.0);
    vec3 sky = mix(horizon, zenith, pow(h, 0.55));
    if (ray.y < 0.0) {
        sky = horizon * (1.0 + ray.y * 0.35);
    }

    float overcast = u_shadow_params.w;
    float sky_luma = dot(sky, vec3(0.30, 0.55, 0.15));
    vec3 haze = mix(vec3(sky_luma), sky, 0.35) * mix(1.0, 0.55, overcast * day);
    sky = mix(sky, haze, overcast);

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
        float twinkle = 0.72 + 0.28 * sin(time * 2.3 + star * 41.0);
        sky += vec3(0.85, 0.9, 1.0) * bright * twinkle * night
             * clamp(ray.y * 3.0, 0.0, 1.0) * (1.0 - overcast);
    }

    if (ray.y > 0.015) {
        vec2 cp = u_cam_pos.xz * 0.0035 + ray.xz / max(ray.y, 0.06) * 1.35;
        cp += vec2(time * 0.0110, time * 0.0042);
        float dcloud = fbm(cp * 0.5);
        float cover = mix(0.62, 0.30, overcast);
        float shape = smoothstep(cover, cover + 0.26, dcloud);
        float fade = smoothstep(0.015, 0.14, ray.y);
        float dense = smoothstep(cover + 0.10, cover + 0.42, dcloud);
        vec3 bright_c = mix(vec3(0.74, 0.76, 0.80), vec3(0.48, 0.50, 0.54), overcast);
        vec3 dark_c = mix(vec3(0.44, 0.46, 0.52), vec3(0.26, 0.28, 0.32), overcast);
        vec3 ccol = mix(bright_c, dark_c, dense);
        ccol *= 0.06 + 0.94 * day;
        ccol += sun_c * pow(max(s, 0.0), 3.0) * (0.18 + 0.55 * dusk) * (1.0 - dense * 0.7);
        sky = mix(sky, ccol, shape * fade * (0.55 + 0.40 * overcast));
    }

    o_color = vec4(sky, 1.0);
}
