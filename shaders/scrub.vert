#version 460 core

layout(location = 1) uniform vec4 u_cam;
layout(location = 2) uniform vec4 u_field;

layout(binding = 3) uniform sampler2D u_roadmask;
layout(binding = 4) uniform sampler2D u_height;

layout(std140, binding = 0) uniform CameraBlock {
    mat4 u_view;
    mat4 u_proj;
    mat4 u_view_proj;
    vec4 u_cam_pos;
    vec4 u_viewport;
    vec4 u_sun_dir;
    vec4 u_sun_color_ambient;
    vec4 u_fog_color_density;
    vec4 u_spot_pos_cone[2];
    vec4 u_spot_dir_intensity[2];
    mat4 u_shadow_mat;
    vec4 u_shadow_params;
    vec4 u_point_pos_radius[4];
    vec4 u_point_color[4];
    vec4 u_sky_ambient;
    vec4 u_ground_ambient;
};

out vec2 v_quad;
out vec3 v_world;
out vec3 v_normal;
out float v_seed;

const float SPACING = 1.15;
const int GRID = 64;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    int inst = gl_InstanceID;
    int gx = inst % GRID;
    int gz = inst / GRID;
    vec2 center = floor(vec2(u_cam.x, u_cam.y) / SPACING);
    vec2 cell = center + vec2(float(gx - GRID / 2), float(gz - GRID / 2));
    float h1 = hash12(cell * 1.013);
    float h2 = hash12(cell * 2.117 + 31.7);
    float h3 = hash12(cell * 3.719 + 77.1);
    vec2 base = (cell + vec2(h1, h2)) * SPACING;

    vec2 uv = (base - u_field.xy) * u_field.zw;
    float height = texture(u_height, uv).r;
    float road = texture(u_roadmask, uv).r;
    float hpx = texture(u_height, uv + vec2(u_field.z, 0.0)).r;
    float hpz = texture(u_height, uv + vec2(0.0, u_field.w)).r;
    vec3 n = normalize(vec3(height - hpx, 1.0, height - hpz));

    float d = length(base - u_cam.xy);
    float fade = 1.0 - smoothstep(u_cam.w * 0.70, u_cam.w, d);
    float keep = step(road, 0.28) * step(0.74, n.y) * step(h3, 0.60);
    keep *= 1.0 - smoothstep(22.0, 30.0, height);
    float scale = fade * keep * mix(0.6, 1.2, h2);

    int vid = gl_VertexID;
    int quad = vid / 6;
    int idx[6] = int[6](0, 1, 2, 0, 2, 3);
    int c = idx[vid % 6];
    vec2 corner = vec2((c == 1 || c == 2) ? 1.0 : 0.0, (c >= 2) ? 1.0 : 0.0);

    float yaw = h1 * 6.2832 + float(quad) * 1.5708;
    vec2 dirv = vec2(cos(yaw), sin(yaw));
    float width = 0.60 * scale;
    float tall = mix(0.30, 0.62, fract(h1 * 7.77)) * scale;
    vec2 xz = base + dirv * (corner.x - 0.5) * width;
    float sway = sin(u_cam.z * 2.1 + h1 * 19.0 + base.x * 0.40 + base.y * 0.31) * 0.09
               + sin(u_cam.z * 3.7 + h2 * 23.0) * 0.035;
    xz += sway * corner.y * corner.y * tall * vec2(0.8, 0.6);

    vec3 wp = vec3(xz.x, height - 0.03 + corner.y * tall, xz.y);
    v_quad = corner;
    v_world = wp;
    v_normal = n;
    v_seed = h1;
    gl_Position = u_view_proj * vec4(wp, 1.0);
}
