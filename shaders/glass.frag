#version 460 core

in vec3 v_world;
in vec3 v_normal;
in vec3 v_local;
in vec3 v_lnormal;

layout(location = 4) uniform vec4 u_rain;

layout(binding = 2) uniform sampler2D u_scene;

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
};

out vec4 o_color;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

float vnoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float water_field(vec2 puv, float time)
{
    float blob = vnoise(puv * vec2(13.0, 9.0) + vec2(0.0, time * 0.05));
    float ripple = vnoise(puv * vec2(50.0, 38.0) + vec2(0.0, time * 0.22));
    float run = vnoise(vec2(puv.x * 44.0, puv.y * 4.5 - time * 0.50));
    return blob * 0.45 + ripple * 0.28 + run * 0.42;
}

void main()
{
    vec3 n = normalize(v_normal);
    vec3 view = normalize(u_cam_pos.xyz - v_world);
    if (dot(n, view) < 0.0) {
        n = -n;
    }
    float fresnel = pow(1.0 - clamp(dot(n, view), 0.0, 1.0), 3.0);

    float day = u_sun_color_ambient.w;
    vec3 rdir = reflect(-view, n);
    vec3 env = mix(u_fog_color_density.rgb,
                   mix(vec3(0.05, 0.08, 0.14), vec3(0.32, 0.46, 0.64), day),
                   clamp(rdir.y * 2.2, 0.0, 1.0));
    float spec = pow(max(dot(rdir, -u_sun_dir.xyz), 0.0), 220.0);

    vec3 tint = vec3(0.35, 0.46, 0.50) * (0.06 + day * 0.35);
    vec3 c = tint + env * fresnel * 0.85 + u_sun_color_ambient.rgb * spec * 2.2;
    float a = clamp(0.14 + 0.55 * fresnel + spec * 0.6, 0.0, 0.85);

    float windshield = step(v_local.z, -0.02);
    float wet = mix(u_rain.z, u_rain.x, windshield);
    float time = u_rain.w;
    if (wet > 0.004) {
        vec3 nl = normalize(v_lnormal);
        vec3 pane_down = normalize(vec3(0.0, -1.0, 0.0) - nl * dot(vec3(0.0, -1.0, 0.0), nl));
        vec3 pane_across = cross(nl, pane_down);
        vec2 puv = vec2(dot(v_local, pane_across), dot(v_local, pane_down));

        float water = water_field(puv, time);
        float e = 0.012;
        vec2 grad = vec2(water_field(puv + vec2(e, 0.0), time) - water,
                         water_field(puv + vec2(0.0, e), time) - water) / e;

        float film = smoothstep(1.04 - wet * 0.42, 1.32 - wet * 0.42, water);

        float wiped = 0.0;
        if (windshield > 0.5) {
            float wiper_arm = 1.17 - u_rain.y * 1.30;
            for (int w = 0; w < 2; w++) {
                vec2 pivot = vec2(w == 0 ? -0.38 : 0.10, 0.094);
                vec2 rel = vec2(v_local.x - pivot.x,
                                dot(v_local - vec3(pivot.x, pivot.y, -0.60), -pane_down));
                float ang = atan(rel.x, rel.y);
                float within = step(length(rel), 0.52);
                wiped = max(wiped, smoothstep(0.30, 0.10, abs(ang - wiper_arm)) * within);
            }
            film *= 1.0 - wiped;
        }

        float base_wobble = wet * (windshield > 0.5 ? 1.0 - wiped : 1.0);
        vec2 offset = grad * (0.0016 * base_wobble + 0.0105 * film);
        vec2 suv = gl_FragCoord.xy / u_viewport.xy;
        vec3 refr = texture(u_scene, clamp(suv + offset, 0.001, 0.999)).rgb;

        float presence = clamp(film + base_wobble * 0.30, 0.0, 1.0);
        c = mix(c, refr * vec3(0.93, 0.96, 0.99), presence);
        c += env * film * 0.05;
        a = clamp(a + presence * 0.85, 0.0, 0.96);
    }

    o_color = vec4(c, a);
}
