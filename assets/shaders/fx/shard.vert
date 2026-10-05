#version 460 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 iPosition;
layout(location = 3) in vec3 iAxis;
layout(location = 4) in vec3 iShape;

uniform mat4 uViewProj;

out vec3 vWorld;
out vec3 vNormal;

void main() {
    vec3 z = normalize(iAxis);
    vec3 helper = abs(z.y) < 0.9 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 x0 = normalize(cross(helper, z));
    vec3 y0 = cross(z, x0);
    float c = cos(iShape.z), s = sin(iShape.z);
    vec3 x = x0 * c + y0 * s;
    vec3 y = y0 * c - x0 * s;
    vec3 size = max(vec3(iShape.y, iShape.y, iShape.x), vec3(1e-4));
    vec3 local = aPosition * size;
    vWorld = iPosition + x * local.x + y * local.y + z * local.z;

    vec3 n = aNormal / size;
    vNormal = normalize(x * n.x + y * n.y + z * n.z);
    gl_Position = uViewProj * vec4(vWorld, 1.0);
}
