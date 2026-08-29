#version 460 core

in vec2 v_uv;

layout(location = 1) uniform vec4 u_params;

layout(binding = 0) uniform sampler2D u_src;

out vec4 o_color;

float karis_w(vec3 c, float base)
{
    return base / (1.0 + dot(c, vec3(0.2126, 0.7152, 0.0722)));
}

void main()
{
    vec2 t = u_params.xy;
    vec3 a = texture(u_src, v_uv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(u_src, v_uv + t * vec2(0.0, -2.0)).rgb;
    vec3 c = texture(u_src, v_uv + t * vec2(2.0, -2.0)).rgb;
    vec3 d = texture(u_src, v_uv + t * vec2(-1.0, -1.0)).rgb;
    vec3 e = texture(u_src, v_uv + t * vec2(1.0, -1.0)).rgb;
    vec3 f = texture(u_src, v_uv + t * vec2(-2.0, 0.0)).rgb;
    vec3 g = texture(u_src, v_uv).rgb;
    vec3 h = texture(u_src, v_uv + t * vec2(2.0, 0.0)).rgb;
    vec3 i = texture(u_src, v_uv + t * vec2(-1.0, 1.0)).rgb;
    vec3 j = texture(u_src, v_uv + t * vec2(1.0, 1.0)).rgb;
    vec3 k = texture(u_src, v_uv + t * vec2(-2.0, 2.0)).rgb;
    vec3 l = texture(u_src, v_uv + t * vec2(0.0, 2.0)).rgb;
    vec3 m = texture(u_src, v_uv + t * vec2(2.0, 2.0)).rgb;

    vec3 col;
    if (u_params.z > 0.5) {
        vec3 g0 = (d + e + i + j) * 0.25;
        vec3 g1 = (a + b + f + g) * 0.25;
        vec3 g2 = (b + c + g + h) * 0.25;
        vec3 g3 = (f + g + k + l) * 0.25;
        vec3 g4 = (g + h + l + m) * 0.25;
        float w0 = karis_w(g0, 0.5);
        float w1 = karis_w(g1, 0.125);
        float w2 = karis_w(g2, 0.125);
        float w3 = karis_w(g3, 0.125);
        float w4 = karis_w(g4, 0.125);
        col = (g0 * w0 + g1 * w1 + g2 * w2 + g3 * w3 + g4 * w4)
            / max(w0 + w1 + w2 + w3 + w4, 1e-4);
    } else {
        col = g * 0.125;
        col += (a + c + k + m) * 0.03125;
        col += (b + f + h + l) * 0.0625;
        col += (d + e + i + j) * 0.125;
    }
    o_color = vec4(col, 1.0);
}
