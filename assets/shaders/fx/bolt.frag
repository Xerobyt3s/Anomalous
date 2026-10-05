#version 460 core

in float vAcross;
in float vReach;
in float vBranch;

out vec4 fragColor;

uniform vec3 uColor;
uniform float uIntensity;
uniform float uBranchIntensity;
uniform float uReveal;
uniform float uCore;

void main() {
    if (vReach > uReveal) discard;
    float a = abs(vAcross);
    float profile = uCore > 0.5 ? 1.0 - smoothstep(0.35, 1.0, a) : exp(-a * a * 4.0) * (1.0 - a);
    vec3 color = uCore > 0.5 ? mix(vec3(1.0), uColor, 0.25) * 6.0 : uColor * 2.5;
    float intensity = uIntensity * mix(1.0, uBranchIntensity, vBranch);

    intensity *= 1.0 + 1.5 * smoothstep(uReveal - 0.06, uReveal, vReach) * step(uReveal, 0.999);
    fragColor = vec4(color * profile * intensity, 1.0);
}
