#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in mat4 a_model;
layout(location = 7) in float a_leaf;

out vec3 v_world;
out vec3 v_normal;
out vec2 v_uv;
out float v_leaf;

vec3 foliage_sway(vec3 local, vec3 world)
{
    if (a_leaf < 0.5) {
        return local;
    }
    float along = dot(vec2(0.86, 0.51), world.xz);
    float sway = sin(along * 0.4 - u_time_seconds * 1.1) * 0.5
               + sin(along * 0.9 - u_time_seconds * 1.9) * 0.25;
    float gust = smoothstep(0.15, 1.0, sin(along * 0.020 - u_time_seconds * 0.33));
    local.xz += vec2(0.86, 0.51) * sway * (0.06 + 0.16 * gust) * local.y * 0.12;
    return local;
}

void main()
{
    vec3 origin = a_model[3].xyz;
    vec3 local = foliage_sway(a_pos, origin + a_pos);
    vec4 world = a_model * vec4(local, 1.0);
    v_world = world.xyz;
    v_normal = mat3(a_model) * a_normal;
    v_uv = a_uv;
    v_leaf = a_leaf;
    gl_Position = u_view_proj * world;
}
