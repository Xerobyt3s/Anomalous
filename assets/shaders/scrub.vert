#version 460 core
#include "common.glsl"
#include "terrain_rock.glsl"
#include "frame.glsl"

layout(location = 1) uniform vec4 u_cam;
layout(location = 2) uniform vec4 u_field;
layout(location = 3) uniform vec4 u_press[16];
layout(location = 19) uniform int u_press_count;
layout(location = 20) uniform vec4 u_lod;
layout(location = 21) uniform int u_lod_level;
layout(location = 22) uniform mat4 u_model;
layout(location = 26) uniform vec3 u_cam_local;
layout(location = 27) uniform int u_patch;
layout(location = 28) uniform ivec4 u_rect;

layout(binding = 3) uniform sampler2D u_roadmask;
layout(binding = 4) uniform sampler2D u_height;

out vec3 v_world;
out vec3 v_terrain_normal;
out vec3 v_blade_normal;
out float v_along;
out float v_jitter;
out float v_dry;
out float v_clump;

const int SLOTS = 8;
const float CLUMP_RADIUS = 0.10;
const int SEGMENTS = 7;
const float HEIGHT_SCALE = 0.40;
const float HEIGHT_MIN = 0.28;
const float HEIGHT_MAX = 0.62;
const float WIDTH_MIN = 0.022;
const float WIDTH_MAX = 0.040;
const float LEAN_MIN = 0.10;
const float LEAN_MAX = 0.60;
const float CURL_MAX = 0.45;
const float FAN = 0.436;
const float LONE_FRACTION = 0.20;
const float DRY_FRACTION = 0.12;
const float WIND_STRENGTH = 0.5;
const float TAPER_POWER = 1.4;
const float SNOW_FLATTEN = 0.55;

vec2 height_uv(vec2 uv)
{
    if (u_patch != 0) {
        return uv;
    }
    vec2 texels = vec2(textureSize(u_height, 0));
    return (uv * (texels - 1.0) + 0.5) / texels;
}

float ground_at(vec2 xz)
{
    return texture(u_height, height_uv((xz - u_field.xy) * u_field.zw)).r;
}

void collapse()
{
    v_world = vec3(0.0);
    v_terrain_normal = vec3(0.0, 1.0, 0.0);
    v_blade_normal = vec3(0.0, 1.0, 0.0);
    v_along = 0.0;
    v_jitter = 0.0;
    v_dry = 0.0;
    v_clump = 0.0;
    gl_Position = vec4(0.0, 0.0, -2.0, 1.0);
}

void main()
{
    float clump_spacing = u_lod.x;
    int clump_grid = int(u_lod.y);
    float level_seed = float(u_lod_level) * 173.31;
    int clump_index = gl_InstanceID / SLOTS;
    int slot = gl_InstanceID % SLOTS;
    vec2 centre_cell = floor(u_cam.xy / clump_spacing);
    vec2 cell = u_rect.z > 0
                    ? centre_cell + vec2(float(u_rect.x + clump_index % u_rect.z),
                                         float(u_rect.y + clump_index / u_rect.z))
                    : centre_cell + vec2(float(clump_index % clump_grid - clump_grid / 2),
                                         float(clump_index / clump_grid - clump_grid / 2));
    vec2 seed_cell = cell + level_seed;

    vec2 clump_centre = (cell + 0.15 + 0.70 * hash22(seed_cell * 1.371 + 4.1)) * clump_spacing;
    if (u_patch != 0) {
        vec2 patch_uv = (clump_centre - u_field.xy) * u_field.zw;
        if (any(lessThan(patch_uv, vec2(0.0))) || any(greaterThan(patch_uv, vec2(1.0)))
            || textureLod(u_height, height_uv(patch_uv), 0.0).r < -7.5) {
            collapse();
            return;
        }
    }
    float ring_distance = length(clump_centre - u_cam.xy);
    float ring = smoothstep(u_lod.z, u_lod.w, ring_distance);
    float ring_keep = u_lod_level == 0 ? 1.0 - ring : ring;
    if (ring_keep <= 0.0) {
        collapse();
        return;
    }
    float patch_noise = fbm2(clump_centre * 0.055, 3) + 0.5;
    float accept = 0.30 + 0.70 * smoothstep(0.22, 0.72, patch_noise);
    if (hash12(seed_cell * 2.913 + 17.3) > accept) {
        collapse();
        return;
    }

    float c1 = hash12(seed_cell * 3.117 + 41.9);
    float c2 = hash12(seed_cell * 4.731 + 8.27);
    float c3 = hash12(seed_cell * 6.203 + 93.1);
    float c4 = hash12(seed_cell * 7.877 + 55.5);
    bool lone = c1 < LONE_FRACTION;
    int count = lone ? 1 : 5 + int(c2 * 3.999);
    if (slot >= count) {
        collapse();
        return;
    }
    float clump_height = mix(HEIGHT_MIN, HEIGHT_MAX, c3);
    float dry = c4 < DRY_FRACTION ? 0.6 + 0.4 * fract(c4 * 31.7) : 0.0;
    float clump_hash = hash12(seed_cell * 9.413 + 2.71);

    vec2 bseed = seed_cell * 1.733 + float(slot) * 12.97;
    float b1 = hash12(bseed + 0.31);
    float b2 = hash12(bseed + 7.77);
    float b3 = hash12(bseed + 13.1);
    float b4 = hash12(bseed + 21.9);
    float b5 = hash12(bseed + 34.3);
    float b6 = hash12(bseed + 47.6);
    float b7 = hash12(bseed + 59.2);
    float b8 = hash12(bseed + 71.5);

    float spread = lone ? 0.0 : CLUMP_RADIUS * sqrt(b1);
    float around = b2 * TAU;
    vec2 base_xz = clump_centre + vec2(cos(around), sin(around)) * spread;
    float yaw = lone ? b3 * TAU : around + (b3 - 0.5) * 2.0 * FAN;

    vec2 uv = (base_xz - u_field.xy) * u_field.zw;
    float ground = texture(u_height, height_uv(uv)).r;
    float road = texture(u_roadmask, uv).r;

    const float e = 2.0;
    float hl = ground_at(base_xz - vec2(e, 0.0));
    float hr = ground_at(base_xz + vec2(e, 0.0));
    float hb = ground_at(base_xz - vec2(0.0, e));
    float hf = ground_at(base_xz + vec2(0.0, e));
    vec3 terrain_normal = normalize(vec3(hl - hr, 2.0 * e, hb - hf));

    vec3 root = vec3(base_xz.x, ground, base_xz.y);
    float height = clump_height * (0.80 + 0.30 * b4) * HEIGHT_SCALE;
    float width = mix(WIDTH_MIN, WIDTH_MAX, b5) * sqrt(HEIGHT_SCALE);
    float outward = lone ? 0.5 : spread / CLUMP_RADIUS;
    float lean = mix(LEAN_MIN, LEAN_MAX, 0.35 * b6 + 0.65 * outward);
    float curl = (b7 - 0.5) * 2.0 * CURL_MAX;
    float phase = b8 * TAU;
    v_jitter = b5;
    v_dry = dry;
    v_clump = clump_hash;

    int level = gl_VertexID >> 1;
    float side = float(gl_VertexID & 1) * 2.0 - 1.0;
    float t = float(level) / float(SEGMENTS);
    v_along = t;

    float dist = length(root - u_cam_local);
    float fade = 1.0 - smoothstep(u_cam.w * 0.72, u_cam.w, dist);
    height *= mix(0.55, 1.0, fade);
    height *= 1.0 - SNOW_FLATTEN * u_snow_cover;
    if (fract(phase * 0.3183 + 0.61) < smoothstep(0.05, 0.85, u_snow_cover)) {
        collapse();
        return;
    }

    float metres_per_pixel = dist * 2.0 * 0.4663 / u_viewport.y;
    float minimum_width = metres_per_pixel * 0.55;
    float inflation = max(1.0, minimum_width / max(width, 1e-4));
    width = max(width, minimum_width) * (fade > 0.0 ? 1.0 : 0.0);

    float keep = pow(1.0 / inflation, 0.75) * fade;
    vec2 to_edge = min(uv, 1.0 - uv) / u_field.zw;
    keep *= u_patch != 0 ? 1.0 : smoothstep(0.0, 70.0, min(to_edge.x, to_edge.y));
    keep *= ring_keep;
    keep *= step(road, 0.30);
    keep *= u_patch != 0 ? step(0.72, terrain_normal.y) * step(-7.5, ground)
                         : 1.0 - smoothstep(0.02, 0.2, terrain_rockiness(base_xz, terrain_normal.y, ground));
    if (fract(phase * 0.7071 + 0.37) > keep) {
        collapse();
        return;
    }

    float squash = 1.0;
    for (int c = 0; c < u_press_count; c++) {
        vec4 box = u_press[c * 2];
        vec4 axes = u_press[c * 2 + 1];
        vec2 d = root.xz - box.xy;
        if (abs(dot(d, axes.xy)) > 1.0 || abs(dot(d, axes.zw)) > 1.0) {
            continue;
        }
        float clearance = box.z - root.y - 0.02;
        squash = min(squash, clamp(clearance / max(height, 1e-3), 0.0, 1.0));
    }
    height *= squash;
    lean += (1.0 - squash) * 1.8;
    if (height < 0.004) {
        collapse();
        return;
    }

    vec3 lean_direction = vec3(cos(yaw), 0.0, sin(yaw));
    vec3 sideways = vec3(-sin(yaw), 0.0, cos(yaw));

    vec2 wind_xz = vec2(0.86, 0.51);
    float along_wind = dot(wind_xz, root.xz);
    float time = u_cam.z;
    float sway = sin(along_wind * 0.55 - time * 1.7 + phase) * 0.5
               + sin(along_wind * 1.30 - time * 2.6 + phase * 1.7) * 0.25;
    float gust = smoothstep(0.15, 1.0, sin(along_wind * 0.020 - time * 0.33));
    float wind_bend = (0.22 + 0.85 * gust) * sway * WIND_STRENGTH * (1.0 + u_wind);
    vec3 wind_direction = vec3(wind_xz.x, 0.0, wind_xz.y);

    float taper = pow(1.0 - t, TAPER_POWER);
    float arch = lean * t * t + curl * t * t * t;
    vec3 position = root + vec3(0.0, height * t, 0.0);
    position += lean_direction * (height * arch);
    position += wind_direction * (height * wind_bend * t * t);
    position += sideways * (side * width * 0.5 * taper);

    vec3 tangent = normalize(vec3(0.0, height, 0.0)
                             + lean_direction * (height * (2.0 * lean * t + 3.0 * curl * t * t))
                             + wind_direction * (2.0 * height * wind_bend * t));
    mat3 basis = mat3(u_model);
    v_blade_normal = normalize(basis * cross(tangent, sideways));
    v_terrain_normal = normalize(basis * terrain_normal);
    v_world = (u_model * vec4(position, 1.0)).xyz;

    gl_Position = u_view_proj * vec4(v_world, 1.0);
}
