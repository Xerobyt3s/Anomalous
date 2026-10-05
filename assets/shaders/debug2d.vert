#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;

out vec4 v_color;

void main()
{
    vec2 ndc = vec2(a_pos.x * u_viewport.z * 2.0 - 1.0,
                    1.0 - a_pos.y * u_viewport.w * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
}
