#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aTangent;
layout(location = 2) in vec4 aParams;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform float uWidth;
uniform float uJitter;
uniform float uJitterSeed;
uniform vec3 uStartShift;
uniform float uStartWidth;

out float vAcross;
out float vReach;
out float vBranch;

vec3 hash3(vec3 p) {
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) - 0.5;
}

void main() {
    float early = 1.0 - smoothstep(0.0, 0.5, aParams.z);
    vec3 p = aPosition + hash3(aPosition * 13.0 + uJitterSeed) * uJitter * (1.0 - early) + uStartShift * early;
    vec3 toCamera = uCameraPos - p;
    float distance = length(toCamera);
    vec3 side = cross(aTangent, toCamera);
    float sideLength = length(side);
    side = sideLength > 1e-5 ? side / sideLength : vec3(1.0, 0.0, 0.0);

    float width = max(uWidth, distance * 0.0016) * 0.5 * mix(uStartWidth, 1.0, smoothstep(0.0, 0.12, aParams.z));
    p += side * aParams.x * width;

    vAcross = aParams.y;
    vReach = aParams.z;
    vBranch = aParams.w;
    gl_Position = uViewProj * vec4(p, 1.0);
}
