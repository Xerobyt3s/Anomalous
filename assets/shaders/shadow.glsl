#ifndef ANOM_SHADOW_GLSL
#define ANOM_SHADOW_GLSL

#include "frame.glsl"

layout(binding = 7) uniform sampler2DShadow u_shadow;

float shadow_factor(vec3 world, float ndl)
{
    if (u_shadow_params.x <= 0.0) {
        return 1.0;
    }
    vec4 sp = u_shadow_mat * vec4(world, 1.0);
    vec3 p = sp.xyz * 0.5 + 0.5;
    if (p.z >= 1.0) {
        return 1.0;
    }
    float bias = clamp(0.0035 * (1.0 - ndl) + 0.0008, 0.0, 0.006);
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += texture(u_shadow, vec3(p.xy + vec2(x, y) * u_shadow_params.y, p.z - bias));
        }
    }
    return mix(1.0, lit / 9.0, u_shadow_params.x);
}

#endif
