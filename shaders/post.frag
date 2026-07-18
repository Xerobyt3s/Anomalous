#version 460 core

in vec2 v_uv;

layout(location = 1) uniform float u_time;
layout(location = 2) uniform vec4 u_shield;

layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_depth;

out vec4 o_color;

const float EXPOSURE = 1.02;
const float SHADOW_LIFT = 0.90;
const float SATURATION = 1.06;
const float VIGNETTE = 0.45;
const float GRAIN = 0.045;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    gl_FragDepth = texture(u_depth, v_uv).r;

    vec3 c = texture(u_color, v_uv).rgb;

    c *= EXPOSURE;
    c = (c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14);
    c = pow(max(c, vec3(0.0)), vec3(SHADOW_LIFT));

    float luma = dot(c, vec3(0.299, 0.587, 0.114));
    c = mix(vec3(luma), c, SATURATION);

    vec2 vc = v_uv - 0.5;
    c *= 1.0 - VIGNETTE * dot(vc, vc);

    float grain = hash12(gl_FragCoord.xy + fract(u_time * 7.31) * 217.0) - 0.5;
    c += grain * GRAIN * (1.0 - luma * 0.6);

    o_color = vec4(c, 1.0);
}
