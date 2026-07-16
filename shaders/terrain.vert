#version 460 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;

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

out vec3 v_world;
out vec3 v_normal;

void main()
{
    v_world = a_pos;
    v_normal = a_normal;
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
