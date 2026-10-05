#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "lighting.glsl"

const float POINT_PARTICLE_GAIN = 0.5;

layout(location = 1) uniform vec4 u_params;
layout(location = 2) uniform vec4 u_cam_vel;
layout(location = 3) uniform vec4 u_cam_fwd;
layout(location = 7) uniform vec4 u_fall_rot;

out vec2 v_uv;
out float v_alpha;
out vec3 v_tint;

void main()
{
    float time = u_params.x;
    float intensity = u_params.y;
    float wind = u_params.z;
    int drop = gl_VertexID / 6;
    int corner = gl_VertexID % 6;
    float id = float(drop);
    int layer = drop % 3;

    float box_size[3] = float[3](16.0, 32.0, 58.0);
    float box_h[3] = float[3](12.0, 18.0, 26.0);
    float width_base[3] = float[3](0.0045, 0.010, 0.030);
    float len_base[3] = float[3](0.55, 0.80, 1.30);
    float alpha_base[3] = float[3](0.34, 0.20, 0.10);
    float near_fade[3] = float[3](0.9, 2.5, 7.0);

    vec3 box = vec3(box_size[layer], box_h[layer], box_size[layer]);
    vec3 seed = vec3(hash11(id + 0.13), hash11(id + 7.7), hash11(id + 41.2));
    float seed2 = hash11(id + 113.7);

    float fall_speed = 15.0 + seed.y * 8.0;
    float gust = 1.0 + 0.45 * sin(time * 0.63 + seed2 * 6.28)
               + 0.20 * sin(time * 1.71 + seed.x * 6.28);
    vec3 fall = quat_rotate(u_fall_rot, vec3(wind * 4.0 * gust, -fall_speed, wind * 1.5 * gust));
    vec3 base = seed * box;
    base += fall * time;

    vec3 anchor = u_cam_pos.xyz + u_cam_fwd.xyz * box.x * 0.30;
    vec3 local = mod(base - anchor + box * 0.5, box) - box * 0.5;
    vec3 world = anchor + local;

    vec3 stretch_dir = normalize(fall - u_cam_vel.xyz * 0.9);
    float len = len_base[layer] * (0.7 + seed.x * 0.6)
              * (0.8 + intensity * 0.5)
              * (1.0 + length(u_cam_vel.xyz) * 0.04);
    vec3 to_cam = u_cam_pos.xyz - world;
    float dist = length(to_cam);
    vec3 side = normalize(cross(stretch_dir, to_cam / max(dist, 1e-3)));
    float width = width_base[layer] * (0.7 + seed.z * 0.7);

    vec2 uv = vec2(corner == 1 || corner == 2 || corner == 4 ? 1.0 : 0.0,
                   corner == 2 || corner == 4 || corner == 5 ? 1.0 : 0.0);
    vec3 pos = world + stretch_dir * (uv.y - 0.5) * len + side * (uv.x - 0.5) * width;

    float fade = smoothstep(near_fade[layer] * 0.5, near_fade[layer], dist)
               * (1.0 - smoothstep(box.x * 0.72, box.x * 0.95, dist));

    float ambient = u_sun_color_ambient.w;
    float sun_lum = dot(u_sun_color_ambient.rgb, vec3(0.30, 0.55, 0.15));
    float lum = 0.35 + ambient * 1.4 + sun_lum * 0.5;
    vec3 tint = mix(u_fog_color_density.rgb, vec3(0.72, 0.78, 0.88), 0.45) * lum;

    tint += pointRadiance(world) * POINT_PARTICLE_GAIN;

    v_uv = uv;
    v_alpha = alpha_base[layer] * (0.6 + seed2 * 0.6) * fade * (0.55 + intensity * 0.45);
    v_tint = tint;

    gl_Position = u_view_proj * vec4(pos, 1.0);
}
