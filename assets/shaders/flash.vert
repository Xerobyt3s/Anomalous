#version 460 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProj;
uniform vec3 uCenter;
uniform vec3 uRight;
uniform vec3 uUp;
uniform float uSize;

out vec2 vLocal;

void main() {
    vLocal = aPosition.xy;
    vec3 world = uCenter + (uRight * aPosition.x + uUp * aPosition.y) * uSize;
    gl_Position = uViewProj * vec4(world, 1.0);
}
