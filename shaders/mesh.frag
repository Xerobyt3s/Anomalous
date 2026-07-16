#version 460 core

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;

layout(binding = 0) uniform sampler2D u_albedo;

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
    vec3 albedo = texture(u_albedo, v_uv).rgb;
    float ndl = max(dot(n, -u_sun_dir.xyz), 0.0);
    vec3 lit = albedo * (u_sun_color_ambient.rgb * ndl + vec3(u_sun_color_ambient.w));
    float dist = length(v_world - u_cam_pos.xyz);
    float fog_amount = 1.0 - exp(-pow(dist * u_fog_color_density.w, 2.0));
    o_color = vec4(mix(lit, u_fog_color_density.rgb, fog_amount), 1.0);
}
