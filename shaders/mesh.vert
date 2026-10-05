#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in mat4 a_model;

out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out vec3 v_up;
out vec3 v_obj;
out vec3 v_obj_normal;
out mat3 v_basis;

void main()
{
    vec4 world = a_model * vec4(a_pos, 1.0);
    v_world = world.xyz;
    v_normal = mat3(a_model) * a_normal;
    v_uv = a_uv;
    v_up = mat3(a_model) * vec3(0.0, 1.0, 0.0);
    v_obj = a_pos;
    v_obj_normal = a_normal;
    v_basis = mat3(normalize(a_model[0].xyz), normalize(a_model[1].xyz), normalize(a_model[2].xyz));
    gl_Position = u_view_proj * world;
}
