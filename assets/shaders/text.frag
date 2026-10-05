#version 460 core

in vec2 v_uv;
in vec4 v_color;

layout(binding = 0) uniform sampler2D u_atlas;

out vec4 o_color;

void main()
{
    o_color = vec4(v_color.rgb, v_color.a * texture(u_atlas, v_uv).r);
}
