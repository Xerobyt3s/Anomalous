#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "atmosphere.glsl"
#include "clouds.glsl"

uniform vec2 uTargetSize;

out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy / uTargetSize;
    vec3 direction = normalize(world_from_depth(uv, 0.999) - u_cam_pos.xyz);
    float jitter = hash12(gl_FragCoord.xy + fract(u_time_seconds * 7.31) * 113.0);
    fragColor = marchClouds(direction, jitter);
}
