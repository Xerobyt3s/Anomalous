#version 460 core

layout(location = 0) uniform vec3 u_channel_color;
layout(location = 1) uniform float u_core_radiance;

in float v_across;
in float v_intensity;

out vec4 o_color;

const float CORE_TIGHTNESS = 62.0;
const float SHEATH_TIGHTNESS = 2.6;
const float SHEATH_WEIGHT = 0.24;

// The core is allowed to clip to white: radiance past about 1 desaturates through the
// tonemap, so the colour has to live in the wide sheath and the bloom halo instead.
void main()
{
    float x = v_across;
    float core = exp(-x * x * CORE_TIGHTNESS);
    float sheath = exp(-x * x * SHEATH_TIGHTNESS) * SHEATH_WEIGHT;
    o_color = vec4(u_channel_color * (u_core_radiance * v_intensity * (core + sheath)), 1.0);
}
