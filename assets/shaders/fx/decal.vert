#version 460 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uNormal;
uniform float uSize;
uniform float uDepth;

void main() {
    vec3 world = uCenter + (uRight * aPosition.x + uUp * aPosition.y) * uSize + uNormal * (aPosition.z * uDepth);
    gl_Position = uViewProj * vec4(world, 1.0);
}
