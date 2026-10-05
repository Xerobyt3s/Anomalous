#version 450 core

layout(binding = 0) uniform sampler2D u_current;
layout(binding = 1) uniform sampler2D u_previous;

layout(location = 0) uniform float u_decay;

in vec2 v_uv;

out vec4 o_color;

void main()
{
    vec3 cur = texture(u_current, v_uv).rgb;
    vec3 prev = textureLod(u_previous, v_uv, 0.0).rgb * u_decay;
    o_color = vec4(max(cur, prev), 1.0);
}
