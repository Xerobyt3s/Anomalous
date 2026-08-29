#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 1) uniform vec4 u_cam;
layout(location = 2) uniform vec4 u_field;

// Volumes that press the grass down, two vec4 per entry:
//   (centre.xz, underside y, unused), then the box's world XZ axes divided by their half
//   extents, so a root is inside when both projections land within [-1, 1].
layout(location = 3) uniform vec4 u_press[16];
layout(location = 19) uniform int u_press_count;

layout(binding = 3) uniform sampler2D u_roadmask;
layout(binding = 4) uniform sampler2D u_height;

out vec3 v_world;
out vec3 v_terrain_normal;
out vec3 v_blade_normal;
out float v_along;
out float v_jitter;

const int GRID = 512;
const float SPACING = 0.16;
const int SEGMENTS = 5;
const float BEND_SCALE = 1.0;
const float WIND_STRENGTH = 1.0;

float ground_at(vec2 xz)
{
    return texture(u_height, (xz - u_field.xy) * u_field.zw).r;
}

void main()
{
    int inst = gl_InstanceID;
    vec2 centre = floor(u_cam.xy / SPACING);
    vec2 cell = centre + vec2(float(inst % GRID - GRID / 2), float(inst / GRID - GRID / 2));

    float h1 = hash12(cell * 1.013);
    float h2 = hash12(cell * 2.117 + 31.7);
    float h3 = hash12(cell * 3.719 + 77.1);
    float h4 = hash12(cell * 5.231 + 12.9);

    vec2 base_xz = (cell + vec2(h1, h2)) * SPACING;
    vec2 uv = (base_xz - u_field.xy) * u_field.zw;
    float ground = texture(u_height, uv).r;
    float road = texture(u_roadmask, uv).r;

    const float e = 1.0;
    float hl = ground_at(base_xz - vec2(e, 0.0));
    float hr = ground_at(base_xz + vec2(e, 0.0));
    float hb = ground_at(base_xz - vec2(0.0, e));
    float hf = ground_at(base_xz + vec2(0.0, e));
    vec3 terrain_normal = normalize(vec3(hl - hr, 2.0 * e, hb - hf));

    vec3 root = vec3(base_xz.x, ground, base_xz.y);
    float yaw = h1 * TAU;
    float phase = h4 * TAU;
    float height = 0.18 + h3 * h3 * 0.55;
    float width = 0.012 + h2 * 0.016;
    v_jitter = h2;

    int level = gl_VertexID >> 1;
    float side = float(gl_VertexID & 1) * 2.0 - 1.0;
    float t = float(level) / float(SEGMENTS);
    v_along = t;

    float dist = length(root - u_cam_pos.xyz);
    float fade = 1.0 - smoothstep(u_cam.w * 0.72, u_cam.w, dist);
    height *= mix(0.55, 1.0, fade);

    float metres_per_pixel = dist * 2.0 * 0.4663 / u_viewport.y;
    float minimum_width = metres_per_pixel * 0.55;
    float inflation = max(1.0, minimum_width / max(width, 1e-4));
    width = max(width, minimum_width) * (fade > 0.0 ? 1.0 : 0.0);

    float keep = pow(1.0 / inflation, 0.75) * fade;
    vec2 to_edge = min(uv, 1.0 - uv) / u_field.zw;
    keep *= smoothstep(0.0, 70.0, min(to_edge.x, to_edge.y));
    keep *= step(road, 0.30);
    keep *= step(0.72, terrain_normal.y);
    keep *= 1.0 - smoothstep(24.0, 32.0, ground);
    if (fract(phase * 0.7071 + 0.37) > keep) {
        height = 0.0;
        width = 0.0;
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
    width *= step(0.004, height);

    vec3 lean = vec3(cos(yaw), 0.0, sin(yaw));
    vec3 sideways = vec3(-sin(yaw), 0.0, cos(yaw));

    vec2 wind_direction = vec2(0.86, 0.51);
    float along_wind = dot(wind_direction, root.xz);
    float time = u_cam.z;
    float sway = sin(along_wind * 0.55 - time * 1.7 + phase) * 0.5
               + sin(along_wind * 1.30 - time * 2.6 + phase * 1.7) * 0.25;
    float gust = smoothstep(0.15, 1.0, sin(along_wind * 0.020 - time * 0.33));
    float bend = (0.22 + 0.85 * gust) * sway * WIND_STRENGTH * BEND_SCALE;
    bend += (fract(phase * 0.1591) - 0.45) * 1.15 * BEND_SCALE;
    bend += (1.0 - squash) * 1.8;

    float taper = 1.0 - t * t * 0.85;
    vec3 position = root;
    position += vec3(0.0, height * t, 0.0);
    position += lean * (height * bend * t * t);
    position += sideways * (side * width * 0.5 * taper);

    vec3 tangent = normalize(vec3(0.0, height, 0.0) + lean * (2.0 * height * bend * t));
    v_blade_normal = normalize(cross(tangent, sideways));
    v_terrain_normal = terrain_normal;
    v_world = position;

    gl_Position = u_view_proj * vec4(position, 1.0);
}
