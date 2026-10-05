#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUv;
layout(location = 2) in float aPart;

uniform mat4 uViewProj;

out vec2 vUv;
out float vPart;

void main() {
    vUv = aUv;
    vPart = aPart;
    gl_Position = uViewProj * vec4(aPosition, 1.0);
}
