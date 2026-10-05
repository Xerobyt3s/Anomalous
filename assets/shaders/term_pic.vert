#version 450 core

layout(location = 0) uniform vec4 u_rect;

out vec2 v_uv;

void main()
{
    vec2 t = vec2(gl_VertexID & 1, (gl_VertexID >> 1) & 1);
    v_uv = t;
    vec2 p = mix(u_rect.xy, u_rect.zw, t);
    gl_Position = vec4(p, 0.0, 1.0);
}
