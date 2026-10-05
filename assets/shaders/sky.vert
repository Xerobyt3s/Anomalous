#version 460 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

out vec2 v_ndc;

void main()
{
    v_ndc = a_pos.xy * 2.0;
    gl_Position = vec4(v_ndc, 1.0, 1.0);
}
