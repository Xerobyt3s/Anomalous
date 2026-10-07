#version 460 core
#include "common.glsl"
#include "frame.glsl"
#include "atmosphere.glsl"
#include "clouds.glsl"

in vec2 v_ndc;

out vec4 o_color;

layout(binding = 14) uniform sampler2D uClouds;
uniform int uCloudsOn;

void main()
{
    vec4 clip = vec4(v_ndc, 1.0, 1.0);
    vec4 world = u_inv_view_proj * clip;
    vec3 direction = normalize(world.xyz / world.w - u_cam_pos.xyz);

    float altitude = worldAltitude(u_cam_pos.xyz);
    vec3 radiance = atmosphereRadiance(altitude, direction, sun_toward(), true);

    float below = smoothstep(0.0, -0.05, direction.y);
    if (below > 0.0) {
        vec3 grazing = normalize(vec3(direction.x, 0.002, direction.z));
        radiance = mix(radiance, atmosphereRadiance(altitude, grazing, sun_toward(), false),
                       below);
    }

    if (uCloudsOn != 0) {
        vec4 cloud = texture(uClouds, gl_FragCoord.xy * u_viewport.zw);
        radiance = radiance * cloud.a + cloud.rgb;
    }

    float overcast = u_shadow_params.w;
    if (overcast > 0.0) {
        float grey = luminance(radiance);
        radiance = mix(radiance, vec3(grey) * mix(1.0, 0.6, overcast), overcast * 0.7);
    }

    o_color = vec4(radiance, 1.0);
}
