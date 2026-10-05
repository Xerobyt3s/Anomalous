#version 460 core
#include "../common.glsl"
#include "../frame.glsl"
#include "../atmosphere.glsl"

uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform float uSplit;

out vec4 fragColor;

const float HAZE_SKY_DISTANCE = 600.0;
const float HAZE_FADE_SPAN = 0.75;
const float HAZE_INSIDE = 0.4;

float hazeOpticalDepth(vec3 ro, vec3 rd, float dist) {
    float depth = 0.0;
    for (int i = 0; i < u_haze_count; i++) {
        vec3 c = u_haze[i].xyz;
        float r = u_haze[i].w;
        vec3 oc = ro - c;
        float core = max(r - u_haze_tint.w, 0.0);
        float outside = mix(HAZE_INSIDE, 1.0, smoothstep(core, core + u_haze_tint.w * HAZE_FADE_SPAN, length(oc)));
        float b = dot(oc, rd);
        float h = b * b - (dot(oc, oc) - r * r);
        if (h <= 0.0) {
            continue;
        }
        h = sqrt(h);
        float t0 = max(-b - h, 0.0);
        float t1 = min(-b + h, dist);
        if (t1 <= t0) {
            continue;
        }
        float closest = length(oc + rd * clamp(-b, t0, t1)) / r;
        depth += (t1 - t0) * (1.0 - closest * closest * 0.7) * outside;
    }
    return depth * u_haze_density;
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
        float depth = hazeOpticalDepth(u_cam_pos.xyz, dir, dist);
        if (depth > 0.0) {
            float t = exp(-depth);
            float lum = dot(u_fog_color_density.rgb, vec3(0.3, 0.55, 0.15));
            vec3 lit = u_haze_tint.rgb * (lum * 1.15 + dot(u_sun_color_ambient.rgb, vec3(0.3, 0.55, 0.15)) * 0.02);
            inscatter = inscatter * t + lit * (1.0 - t);
            transmittance *= t;
            haze = 1.0 - t;
        }
    }
    color = color * transmittance + inscatter;

    color *= u_exposure;
    if (any(isnan(color)) || any(isinf(color))) {
        color = vec3(0.0);
    }
    color = max(color, vec3(0.0));
    color = color / (1.0 + color);
    fragColor = vec4(pow(color, vec3(1.0 / 2.2)), haze);
}
