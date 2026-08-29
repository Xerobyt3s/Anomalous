#version 460 core

in vec2 v_uv;

layout(location = 1) uniform vec4 u_params;

layout(binding = 0) uniform sampler2D u_src;

out vec4 o_color;

void main()
{
    vec2 t = u_params.xy;
    vec3 col = texture(u_src, v_uv).rgb * 4.0;
    col += texture(u_src, v_uv + vec2(-t.x, 0.0)).rgb * 2.0;
    col += texture(u_src, v_uv + vec2(t.x, 0.0)).rgb * 2.0;
    col += texture(u_src, v_uv + vec2(0.0, -t.y)).rgb * 2.0;
    col += texture(u_src, v_uv + vec2(0.0, t.y)).rgb * 2.0;
    col += texture(u_src, v_uv + vec2(-t.x, -t.y)).rgb;
    col += texture(u_src, v_uv + vec2(t.x, -t.y)).rgb;
    col += texture(u_src, v_uv + vec2(-t.x, t.y)).rgb;
    col += texture(u_src, v_uv + vec2(t.x, t.y)).rgb;
    o_color = vec4(col / 16.0, 1.0);
}
