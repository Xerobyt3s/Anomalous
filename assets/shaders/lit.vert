#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in mat4 aModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUv;
out vec3 vUp;
out vec3 vObjPos;
out vec3 vObjNormal;
out mat3 vBasis;

void main() {
    vec4 world = aModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(aModel) * aNormal;
    vUv = aUv;
    vUp = mat3(aModel) * vec3(0.0, 1.0, 0.0);
    vObjPos = aPosition;
    vObjNormal = aNormal;
    vBasis = mat3(normalize(aModel[0].xyz), normalize(aModel[1].xyz), normalize(aModel[2].xyz));
    gl_Position = u_view_proj * world;
}
