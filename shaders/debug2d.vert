#version 460 core

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

layout(std140, binding = 0) uniform CameraBlock {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_viewport;
};

out vec4 v_color;

void main()
{
    vec2 ndc = vec2(a_pos.x * u_viewport.z * 2.0 - 1.0,
                    1.0 - a_pos.y * u_viewport.w * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
