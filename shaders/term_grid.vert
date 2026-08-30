#version 450 core

layout(location = 0) in vec4 i_pos_shade;

layout(location = 0) uniform mat4 u_view_proj;

out vec3 v_color;

const vec3 DIM = vec3(0.09, 0.38, 0.14);
const vec3 HOT = vec3(0.64, 1.00, 0.72);

void main()
{
    v_color = mix(DIM, HOT, clamp(i_pos_shade.w, 0.0, 1.0));
    gl_Position = u_view_proj * vec4(i_pos_shade.xyz, 1.0);
    gl_Position.y = -gl_Position.y;
}
