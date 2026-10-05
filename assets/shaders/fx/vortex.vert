#version 460 core

layout(location = 0) in vec3 aP;
layout(location = 2) in vec2 aUv;

uniform mat4 uViewProj;
uniform vec3 uBase;
uniform vec3 uAxis;
uniform vec3 uB;
uniform vec3 uC;
uniform float uRadius;
uniform float uHeight;
uniform float uFunnel;
uniform float uTime;

out vec2 vUV;

void main() {
    float prof = mix(1.0, 0.30 + 0.70 * pow(aP.y, 1.6), uFunnel);
    float wob = 0.12 * aP.y * uRadius;
    float x = aP.x * uRadius * prof + wob * sin(uTime * 2.3 + aP.y * 4.0);
    float z = aP.z * uRadius * prof + wob * cos(uTime * 1.9 + aP.y * 3.0);
    vec3 p = uBase + uB * x + uAxis * (aP.y * uHeight) + uC * z;
    vUV = aUv;
    gl_Position = uViewProj * vec4(p, 1.0);
}
