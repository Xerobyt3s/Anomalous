#version 460 core

layout(location = 0) in vec3 aPosition;

struct Skid {
    vec4 centerStrength;
    vec4 alongHalfLength;
    vec4 normalHalfWidth;
    vec4 color;
};

layout(std430, binding = 3) readonly buffer Skids {
    Skid skids[];
};

uniform mat4 uViewProj;
uniform float uDepth;

flat out int vIndex;

void main() {
    Skid s = skids[gl_InstanceID];
    vec3 n = s.normalHalfWidth.xyz;
    vec3 along = s.alongHalfLength.xyz;
    vec3 across = cross(n, along);
    across = dot(across, across) > 1e-8 ? normalize(across) : vec3(1.0, 0.0, 0.0);
    vec3 world = s.centerStrength.xyz + along * (aPosition.x * s.alongHalfLength.w)
               + across * (aPosition.y * s.normalHalfWidth.w) + n * (aPosition.z * uDepth);
    vIndex = gl_InstanceID;
    gl_Position = uViewProj * vec4(world, 1.0);
}
