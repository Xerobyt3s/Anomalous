#version 460 core

in vec4 vColor;
in float vGlow;
in float vAcross;
out vec4 fragColor;

void main() {
    float edge = 1.0 - smoothstep(0.55, 1.0, abs(vAcross));
    float cover = clamp(vColor.a * edge, 0.0, 1.0);
    vec3 rgb = vColor.rgb * cover + vColor.rgb * vGlow * edge * (1.0 - abs(vAcross) * 0.5);
    fragColor = vec4(max(rgb, vec3(0.0)), cover);
}
