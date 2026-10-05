#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aParams;

uniform mat4 uViewProj;

out vec4 vColor;
out float vGlow;
out float vAcross;

void main() {
    vColor = aColor;
    vGlow = aParams.x;
    vAcross = aParams.y;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
