#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;

out vec3 v_world;
out vec3 v_normal;

void main()
{
    v_world = a_pos;
    v_normal = a_normal;
    gl_Position = u_view_proj * vec4(a_pos, 1.0);
}
