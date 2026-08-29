#version 460 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

out vec2 v_uv;

void main()
{
    v_uv = a_pos.xy + 0.5;
    gl_Position = vec4(a_pos.xy * 2.0, 0.0, 1.0);
}
