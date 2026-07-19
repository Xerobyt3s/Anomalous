#version 460 core

in vec2 v_quad;
in vec3 v_world;
in vec3 v_normal;
in float v_seed;

layout(binding = 0) uniform sampler2D u_grass;
layout(binding = 7) uniform sampler2DShadow u_shadow;

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

out vec4 o_color;

float shadow_factor(vec3 world)
{
    if (u_shadow_params.x <= 0.0) {
        return 1.0;
    }
    vec4 sp = u_shadow_mat * vec4(world, 1.0);
    vec3 p = sp.xyz * 0.5 + 0.5;
    if (p.z >= 1.0) {
        return 1.0;
    }
    float lit = texture(u_shadow, vec3(p.xy, p.z - 0.0030));
    return mix(1.0, lit, u_shadow_params.x);
}

vec3 apply_fog(vec3 lit, vec3 world)
{
    vec3 to_frag = world - u_cam_pos.xyz;
    float dist = length(to_frag);
    float height_falloff = exp(-max(world.y - 6.0, 0.0) * 0.035);
    float density = u_fog_color_density.w * mix(0.55, 1.0, height_falloff);
    float fog_amount = 1.0 - exp(-pow(dist * density, 2.0));
    vec3 vdir = to_frag / max(dist, 1e-4);
    float sun_glow = pow(max(dot(vdir, -u_sun_dir.xyz), 0.0), 9.0);
    vec3 fog_c = u_fog_color_density.rgb
               + u_sun_color_ambient.rgb * sun_glow * 0.45 * (1.0 - u_shadow_params.w);
    return mix(lit, fog_c, fog_amount);
}

void main()
{
    float mask = 0.0;
    for (int i = 0; i < 5; i++) {
        float fi = float(i);
        float r0 = fract(v_seed * 13.71 + fi * 0.371);
        float r1 = fract(v_seed * 7.13 + fi * 0.737);
        float r2 = fract(v_seed * 3.97 + fi * 0.531);
        float x0 = 0.10 + 0.80 * r0;
        float lean = (r1 - 0.5) * 0.45;
        float hmax = 0.50 + 0.50 * r2;
        float y = v_quad.y / hmax;
        if (y > 1.0) {
            continue;
        }
        float dx = v_quad.x - x0 - lean * y * y;
        float hw = 0.045 * (1.0 - y * 0.85);
        mask = max(mask, step(abs(dx), hw));
    }
    if (mask < 0.5) {
        discard;
    }

    vec3 gcol = texture(u_grass, v_world.xz * 0.16).rgb;
    vec3 albedo = gcol * mix(0.45, 1.30, v_quad.y);
    albedo = mix(albedo, albedo * vec3(1.14, 1.04, 0.62), v_quad.y * 0.35);

    float wetness = u_shadow_params.z;
    albedo *= 1.0 - wetness * 0.28;

    vec3 n = normalize(v_normal);
    vec3 to_sun = -u_sun_dir.xyz;
    float wrap = clamp(dot(n, to_sun) * 0.5 + 0.5, 0.0, 1.0);
    float shadow = shadow_factor(v_world);
    vec3 hemi = mix(u_ground_ambient.rgb, u_sky_ambient.rgb, 0.75);
    vec3 lit = albedo * (u_sun_color_ambient.rgb * wrap * shadow + hemi);

    for (int i = 0; i < 2; i++) {
        if (u_spot_dir_intensity[i].w > 0.0) {
            vec3 to_frag = v_world - u_spot_pos_cone[i].xyz;
            float dist = max(length(to_frag), 1e-4);
            vec3 l = to_frag / dist;
            float cone = smoothstep(u_spot_pos_cone[i].w, u_spot_pos_cone[i].w + 0.10,
                                    dot(l, u_spot_dir_intensity[i].xyz));
            float atten = u_spot_dir_intensity[i].w / (1.0 + 0.022 * dist * dist);
            lit += albedo * vec3(1.0, 0.93, 0.74) * (cone * atten * 0.8);
        }
    }

    o_color = vec4(apply_fog(lit, v_world), 1.0);
}
