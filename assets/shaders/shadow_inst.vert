#version 460 core

layout(location = 0) in vec3 a_pos;
layout(location = 3) in mat4 a_model;

layout(location = 0) uniform mat4 u_light_vp;

void main()
{
    gl_Position = u_light_vp * a_model * vec4(a_pos, 1.0);
}
