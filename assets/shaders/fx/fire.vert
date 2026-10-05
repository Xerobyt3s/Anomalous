#version 460 core

layout(location = 0) in vec3 aCenter;
layout(location = 1) in vec2 aCorner;
layout(location = 2) in vec4 aParams;

uniform mat4 uViewProj;
uniform vec3 uRight;
uniform vec3 uUp;

out vec2 vLocal;
out vec4 vP;

void main() {
    vLocal = aCorner;
    vP = aParams;
    vec3 p = aCenter + uRight * aCorner.x * aParams.x * 0.5 + uUp * aCorner.y * aParams.x * 0.72;
    gl_Position = uViewProj * vec4(p, 1.0);
}
