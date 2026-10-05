#version 460 core

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) uniform vec4 u_rect;

out vec2 v_uv;

void main()
{
    vec2 t = a_pos.xy + 0.5;
    vec2 ndc = mix(u_rect.xy, u_rect.zw, t);
    v_uv = a_uv;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
