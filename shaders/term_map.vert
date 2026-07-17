#version 450 core

layout(location = 0) in vec4 i_pos_kind;

layout(location = 0) uniform mat4 u_view_proj;
layout(location = 4) uniform vec3 u_car;
layout(location = 5) uniform float u_time;
layout(location = 6) uniform float u_sweep;
layout(location = 7) uniform float u_reveal;

out vec3 v_color;
out float v_alpha;

const float TAU = 6.28318530;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec3 pos = i_pos_kind.xyz;
    float kind = i_pos_kind.w;
    vec2 rel = pos.xz - u_car.xz;
    float dist = length(rel);
    float ang = atan(rel.y, rel.x);
    float behind = fract((u_sweep - ang) / TAU);
    float trail = exp(-behind * 4.0);
    float ring = exp(-abs(dist - mod(u_time * 34.0, 320.0)) * 0.09);
    float flick = 0.82 + 0.36 * hash12(pos.xz + floor(u_time * 9.0));

    v_alpha = 1.0;
    gl_PointSize = 2.0;

    if (kind < 1.5) {
        float bright = (0.28 + 0.85 * trail + 0.40 * ring) * flick;
        bright += exp(-abs(dist - u_reveal) * 0.12) * 1.1;
        float rel_h = clamp((pos.y - u_car.y + 14.0) / 28.0, 0.0, 1.0);
        vec3 base = mix(vec3(0.07, 0.34, 0.12), vec3(0.55, 1.0, 0.62), rel_h);
        if (kind > 0.5) {
            base = vec3(0.86, 0.72, 0.28);
            bright += 0.25;
        }
        v_color = base * bright;
        gl_PointSize = 2.0 + trail * 1.6;
        if (dist > u_reveal) {
            v_alpha = 0.0;
        }
        float fade = clamp(1.0 - (dist - 380.0) / 120.0, 0.0, 1.0);
        v_color *= fade;
    } else if (kind < 2.5) {
        v_color = vec3(0.90, 0.66, 0.24) * (0.85 + 0.35 * sin(u_time * 2.2 + pos.y * 0.4));
        gl_PointSize = 3.0;
    } else {
        float blink = step(0.5, fract(u_time * 1.4));
        v_color = vec3(1.0, 0.42, 0.22) * (0.35 + 0.95 * blink);
        gl_PointSize = 3.0;
    }

    gl_Position = u_view_proj * vec4(pos, 1.0);
    gl_Position.y = -gl_Position.y;
}
