#version 460 core

in vec3 v_world;
in vec3 v_normal;

layout(location = 0) uniform vec4 u_terrain;

layout(binding = 0) uniform sampler2D u_grass;
layout(binding = 1) uniform sampler2D u_rock;
layout(binding = 2) uniform sampler2D u_road;
layout(binding = 3) uniform sampler2D u_roadmask;

layout(std140, binding = 0) uniform CameraBlock {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_viewport;
    vec4 u_sun_dir;
    vec4 u_sun_color_ambient;
    vec4 u_fog_color_density;
};

out vec4 o_color;

void main()
{
    vec3 n = normalize(v_normal);

    vec3 grass = texture(u_grass, v_world.xz * 0.16).rgb;
    vec3 rock = texture(u_rock, v_world.xz * 0.11).rgb;
    vec3 road = texture(u_road, v_world.xz * 0.28).rgb;

    float rockiness = 1.0 - smoothstep(0.6, 0.85, n.y);
    rockiness = clamp(rockiness + smoothstep(24.0, 34.0, v_world.y) * 0.55, 0.0, 1.0);
    vec3 albedo = mix(grass, rock, rockiness);

    vec2 mask_uv = (v_world.xz - u_terrain.xy) * u_terrain.zw;
    float road_amount = texture(u_roadmask, mask_uv).r;
    albedo = mix(albedo, road, road_amount);

    float ndl = max(dot(n, -u_sun_dir.xyz), 0.0);
    vec3 lit = albedo * (u_sun_color_ambient.rgb * ndl + vec3(u_sun_color_ambient.w));
    float dist = length(v_world - u_cam_pos.xyz);
    float fog_amount = 1.0 - exp(-pow(dist * u_fog_color_density.w, 2.0));
    o_color = vec4(mix(lit, u_fog_color_density.rgb, fog_amount), 1.0);
}
