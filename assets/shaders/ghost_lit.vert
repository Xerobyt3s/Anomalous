#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;

uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProj;
uniform mat4 uMapSpace;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec3 vObjPos;
out vec3 vObjNormal;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    vObjPos = (uMapSpace * vec4(aPosition, 1.0)).xyz;
    vObjNormal = mat3(uMapSpace) * aNormal;
    gl_Position = uViewProj * world;
}
