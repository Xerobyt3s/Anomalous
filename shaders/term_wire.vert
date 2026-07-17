#version 450 core

layout(location = 0) in vec3 i_pos;

layout(location = 0) uniform mat4 u_mvp;

void main()
{
    gl_Position = u_mvp * vec4(i_pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
