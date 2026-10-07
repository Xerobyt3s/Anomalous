#version 460 core
#include "../common.glsl"
#include "../frame.glsl"
#include "../atmosphere.glsl"
#include "../haze.glsl"

uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform float uSplit;

out vec4 fragColor;

const float kOvercastMist = 0.0012;
const float kRainMist = 0.0045;
const float kSnowMist = 0.016;
const float kSkyMistDistance = 2500.0;

float weatherExtinction() {
    return u_shadow_params.w * kOvercastMist + u_shadow_params.z * kRainMist + u_snow_fall * kSnowMist;
}

vec3 weatherFogColor(vec3 dir) {
    float glow = pow(max(dot(dir, sun_toward()), 0.0), 8.0) * (1.0 - u_shadow_params.w);
    return u_ambient_horizon.rgb * (1.0 - 0.3 * u_shadow_params.w) + u_sun_color_ambient.rgb * (0.04 * glow);
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(uScene, 0);
    vec2 uv = (vec2(p) + 0.5) / vec2(size);
    vec3 color = texelFetch(uScene, p, 0).rgb;
    float raw = texelFetch(uDepth, p, 0).r;

    vec3 transmittance = vec3(1.0);
    vec3 inscatter = vec3(0.0);
    float haze = 0.0;
    vec3 dir;
    float dist;
    bool world = raw >= uSplit;
    if (world && raw < 1.0) {
        vec3 toFrag = world_from_depth(uv, (raw - uSplit) / (1.0 - uSplit)) - u_cam_pos.xyz;
        dist = length(toFrag);
        dir = toFrag / max(dist, 1e-4);
        inscatter = aerialPerspective(worldAltitude(u_cam_pos.xyz), dir, sun_toward(), dist * 0.001, transmittance);
    } else {
        dir = normalize(world_from_depth(uv, 0.999) - u_cam_pos.xyz);
        dist = HAZE_SKY_DISTANCE;
    }
    if (world && u_haze_count > 0) {
        float depth = hazeOpticalDepth(u_cam_pos.xyz, dir, dist, 0.0, dist);
        if (depth > 0.0) {
            haze = 1.0 - exp(-depth);
        }
    }
    color = color * transmittance + inscatter;

    float extinction = weatherExtinction();
    if (extinction > 0.0) {
        float mist = world && raw < 1.0
                         ? 1.0 - exp(-extinction * dist)
                         : (1.0 - exp(-extinction * kSkyMistDistance)) * (1.0 - smoothstep(0.0, 0.3, dir.y));
        color = mix(color, weatherFogColor(dir), mist);
    }

    color *= u_exposure;
    if (any(isnan(color)) || any(isinf(color))) {
        color = vec3(0.0);
    }
    color = max(color, vec3(0.0));
    color = color / (1.0 + color);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), haze);
}
