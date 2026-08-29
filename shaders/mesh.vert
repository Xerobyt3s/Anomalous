#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 0) uniform mat4 u_model;

out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;

void main()
{
    vec4 world = u_model * vec4(a_pos, 1.0);
    v_world = world.xyz;
    v_normal = mat3(u_model) * a_normal;
    v_uv = a_uv;
    gl_Position = u_view_proj * world;
}
