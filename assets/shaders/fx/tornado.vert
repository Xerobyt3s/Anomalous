#version 460 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;
uniform vec3 uBoxCenter;
uniform vec3 uBoxHalf;
uniform mat3 uBasis;

out vec3 vWorld;

void main() {
    vWorld = uBoxCenter + uBasis * (aPosition * uBoxHalf);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
