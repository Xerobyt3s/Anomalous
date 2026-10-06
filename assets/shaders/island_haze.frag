#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "haze.glsl"

uniform sampler2D uDepth;
uniform float uSplit;
uniform vec2 uSpan;

out vec4 fragColor;

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uDepth, 0);
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    float raw = texelFetch(uDepth, p, 0).r;
    if (raw < uSplit) {
        discard;
    }
    vec3 dir;
    float dist;
    if (raw < 1.0) {
        vec3 toFrag = world_from_depth(uv, (raw - uSplit) / (1.0 - uSplit)) - u_cam_pos.xyz;
        dist = length(toFrag);
        dir = toFrag / max(dist, 1e-4);
    } else {
        dir = normalize(world_from_depth(uv, 0.999) - u_cam_pos.xyz);
        dist = HAZE_SKY_DISTANCE;
    }
    float t0 = max(uSpan.x, 0.0);
    float t1 = min(uSpan.y, dist);
    if (!(t1 > t0)) {
        discard;
    }
    float depth = hazeOpticalDepth(u_cam_pos.xyz, dir, dist, t0, t1);
    if (!(depth > 1e-5)) {
        discard;
    }
    float a = 1.0 - exp(-depth);
    vec3 c = hazeLight() * a;
    if (any(isnan(c)) || any(isinf(c)) || isnan(a)) {
        discard;
    }
    fragColor = vec4(c, a);
}
