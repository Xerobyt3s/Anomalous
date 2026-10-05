#version 460 core
#include "common.glsl"
#include "frame.glsl"

layout(location = 1) uniform vec4 u_params;
layout(location = 2) uniform vec4 u_wind_vel;
layout(location = 7) uniform vec4 u_fall_rot;

out vec2 v_local;
out float v_alpha;
out vec3 v_tint;

const vec3 BOX_NEAR = vec3(14.0, 8.0, 14.0);
const vec3 BOX_MID = vec3(60.0, 24.0, 60.0);
const float FALL_MIN = 1.2;
const float FALL_MAX = 1.9;
const float FLAKE_SIZE = 0.012;
const float MIN_WIDTH_PIXELS = 2.4;
const float MIN_LENGTH_PIXELS = 2.6;
const float SHUTTER = 1.0 / 60.0;
const float ENERGY_POWER = 0.7;
const float WOBBLE = 0.35;
const float TURBULENCE = 3.0;
const float TURBULENCE_SCALE = 1.0 / 20.0;
const float ALBEDO = 0.82;

uint pcg(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float unit_from(uint v)
{
    return float(v) * (1.0 / 4294967296.0);
}

void main()
{
    float time = u_params.x;
    float intensity = u_params.y;
    int near_count = int(u_params.w);
    vec2 wind = u_wind_vel.xy;

    int id = gl_InstanceID;
    bool near = id < near_count;
    vec3 box = near ? BOX_NEAR : BOX_MID;

    uint state = pcg(uint(id));
    vec3 h;
    h.x = unit_from(state); state = pcg(state);
    h.y = unit_from(state); state = pcg(state);
    h.z = unit_from(state); state = pcg(state);
    vec3 h2;
    h2.x = unit_from(state); state = pcg(state);
    h2.y = unit_from(state); state = pcg(state);
    h2.z = unit_from(state);
    vec3 offset = (h * 2.0 - 1.0) * box;

    float fall = mix(FALL_MIN, FALL_MAX, h2.x);
    vec3 velocity = quat_rotate(u_fall_rot, vec3(wind.x, -fall, wind.y));

    vec3 centre = u_cam_pos.xyz;
    vec3 drifted = offset + velocity * time;
    vec3 world = mod(drifted - centre + box, box * 2.0) - box + centre;

    vec2 wind_dir = length(wind) > 1e-3 ? normalize(wind) : vec2(1.0, 0.0);
    vec3 across = vec3(-wind_dir.y, 0.0, wind_dir.x);
    float phase = h2.y * TAU;
    float rate = mix(1.5, 3.0, h2.z);
    world += across * (sin(time * rate + phase) * WOBBLE);
    world.y += cos(time * rate * 0.7 + phase) * WOBBLE * 0.5;

    vec2 q = (world.xz - wind * time * 0.5) * TURBULENCE_SCALE;
    const float e = 0.05;
    float psi0 = fbm2(q, 2);
    float dpsi_dx = (fbm2(q + vec2(e, 0.0), 2) - psi0) / e;
    float dpsi_dz = (fbm2(q + vec2(0.0, e), 2) - psi0) / e;
    world.xz += vec2(dpsi_dz, -dpsi_dx) * (TURBULENCE * TURBULENCE_SCALE);
    world.y += fbm2(q + vec2(3.1, 5.7), 2) * TURBULENCE * 0.3;

    vec3 to_camera = centre - world;
    float dist = length(to_camera);
    vec3 view_direction = to_camera / max(dist, 1e-4);
    float metres_per_pixel = dist * 2.0 / (u_proj[1][1] * u_viewport.y);

    vec3 along = normalize(velocity);
    vec3 side = cross(along, view_direction);
    float side_length = length(side);
    side = side_length > 1e-4 ? side / side_length : normalize(cross(along, vec3(1.0, 0.0, 0.0)));

    float streak = max(length(velocity) * SHUTTER, metres_per_pixel * MIN_LENGTH_PIXELS);
    float half_width = max(FLAKE_SIZE, metres_per_pixel * MIN_WIDTH_PIXELS) * 0.5;

    float s = float(gl_VertexID & 1) * 2.0 - 1.0;
    float run = float(gl_VertexID >> 1);
    vec3 position = world + side * (s * half_width) - along * (run * streak);

    float fade = smoothstep(1.5, 5.0, dist) * (1.0 - smoothstep(box.x * 0.80, box.x, dist));
    float true_length = max(length(velocity) * SHUTTER, FLAKE_SIZE);
    float energy = clamp((FLAKE_SIZE * true_length) / (2.0 * half_width * streak), 0.0, 1.0);
    float dim = pow(energy, ENERGY_POWER);

    vec3 sky_light = ambient_for_normal(vec3(0.0, 1.0, 0.0));
    vec3 bounce = mix(u_ground_ambient.rgb, sky_light * 0.8, u_snow_cover);
    vec3 light = sky_light * 0.75 + bounce * 0.5;
    light += u_sun_color_ambient.rgb * INV_PI * 0.6;
    for (int sp = 0; sp < 2; sp++) {
        if (u_spot_dir_intensity[sp].w > 0.0) {
            vec3 to_frag = world - u_spot_pos_cone[sp].xyz;
            float sd = max(length(to_frag), 1e-3);
            vec3 l = to_frag / sd;
            float cone = smoothstep(u_spot_pos_cone[sp].w, u_spot_pos_cone[sp].w + 0.12,
                                    dot(l, u_spot_dir_intensity[sp].xyz));
            float atten = u_spot_dir_intensity[sp].w / (1.0 + 0.06 * sd * sd);
            light += vec3(1.0, 0.93, 0.74) * cone * atten * 0.55;
        }
    }
    for (int p = 0; p < 4; p++) {
        if (u_point_pos_radius[p].w > 0.0) {
            float pd = length(world - u_point_pos_radius[p].xyz);
            float x = clamp(1.0 - pd / u_point_pos_radius[p].w, 0.0, 1.0);
            light += u_point_color[p].rgb * x * x * 1.5;
        }
    }

    v_local = vec2(s, run);
    v_alpha = fade * dim * 0.75 * mix(0.6, 1.0, intensity);
    v_tint = ALBEDO * light;
    gl_Position = u_view_proj * vec4(position, 1.0);
}
