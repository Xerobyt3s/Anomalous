#version 460 core

in vec3 v_world;
in vec3 v_normal;
in vec2 v_uv;

layout(location = 4) uniform float u_time;
layout(location = 5) uniform float u_power;
layout(location = 6) uniform float u_burn;
layout(location = 7) uniform float u_shake;
layout(location = 8) uniform float u_pixelate;
layout(binding = 0) uniform sampler2D u_tex;

out vec4 o_color;

const float PI = 3.14159265;
const vec3 PHOSPHOR = vec3(0.36, 1.0, 0.47);

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 443.8975);
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 crt(vec2 uv, float time, float power)
{
    vec2 c = uv * 2.0 - 1.0;
    c *= 0.86;
    c *= 1.0 + 0.11 * dot(c, c);
    uv = c * 0.5 + 0.5;

    float collapse = smoothstep(0.0, 0.45, power);
    float v_open = mix(0.006, 1.0, pow(collapse, 3.0));
    uv.y = (uv.y - 0.5) / v_open + 0.5;

    float degauss = exp(-max(power - 0.40, 0.0) * 2.6) * step(0.40, power);
    uv.x += sin(uv.y * 30.0 + power * 45.0) * 0.012 * degauss;
    uv.x += (hash12(vec2(floor(time * 83.0), 5.2)) - 0.5) * u_shake * 0.05;
    uv.y += (hash12(vec2(4.7, floor(time * 89.0))) - 0.5) * u_shake * 0.05;

    vec2 q = abs(uv * 2.0 - 1.0) - 0.94;
    float corner = 1.0 - smoothstep(0.033, 0.06, length(max(q, 0.0)));
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return vec3(0.004, 0.01, 0.005);
    }

    if (u_pixelate > 1.5) {
        vec2 src = vec2(640.0, 400.0);
        uv = (floor(uv * src / u_pixelate) + 0.5) * u_pixelate / src;
    }

    vec3 color = textureLod(u_tex, uv, 0.0).rgb;
    vec3 glow = textureLod(u_tex, uv, 2.0).rgb * 0.7 + textureLod(u_tex, uv, 3.5).rgb * 0.5;
    color += glow * 1.05;

    float s = sin(uv.y * 360.0 * PI);
    color *= 1.0 - 0.28 * s * s;
    float mask_col = mod(floor(gl_FragCoord.x), 3.0);
    color *= 1.0 - 0.10 * step(1.5, mask_col);

    vec2 vc = uv - 0.5;
    color *= 1.0 - 0.42 * dot(vc, vc);
    color *= 1.0 + 0.022 * sin(time * 120.0) * (0.6 + 0.4 * sin(time * 7.3));
    float grain = hash12(uv * vec2(640.0, 400.0) + fract(time) * 117.0) - 0.5;
    color += grain * 0.045 * (0.25 + color.g);

    float d = abs(v_uv.y - 0.5);
    float beam = exp(-d * d / max(0.5 * v_open * v_open, 1e-5));
    color += PHOSPHOR * beam * (1.0 - collapse) * 2.0;

    if (u_burn > 0.001) {
        vec2 bc = uv * 2.0 - 1.0;
        float burn_edge = clamp(dot(bc, bc) * 1.2 - 0.15, 0.0, 1.2);
        float burn_flick = 0.75 + 0.5 * hash12(uv * 91.0 + fract(time) * 57.0);
        color += vec3(1.0, 0.40, 0.10) * u_burn * (burn_edge * 0.9 + 0.08) * burn_flick;
    }

    float warm = 0.2 + 0.8 * smoothstep(0.0, 2.4, power);
    return color * corner * warm;
}

void main()
{
    o_color = vec4(crt(v_uv, u_time, u_power), 1.0);
}
