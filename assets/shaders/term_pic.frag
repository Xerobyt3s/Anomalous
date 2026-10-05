#version 450 core

layout(binding = 0) uniform sampler2D u_pic;

layout(location = 1) uniform float u_reveal;

in vec2 v_uv;

out vec4 o_color;

const float BAYER[16] = float[16](
     0.0 / 16.0,  8.0 / 16.0,  2.0 / 16.0, 10.0 / 16.0,
    12.0 / 16.0,  4.0 / 16.0, 14.0 / 16.0,  6.0 / 16.0,
     3.0 / 16.0, 11.0 / 16.0,  1.0 / 16.0,  9.0 / 16.0,
    15.0 / 16.0,  7.0 / 16.0, 13.0 / 16.0,  5.0 / 16.0);

void main()
{
    vec2 dims = vec2(textureSize(u_pic, 0));
    vec2 px = floor(v_uv * dims);
    vec3 c = texture(u_pic, (px + 0.5) / dims).rgb;
    float g = dot(c, vec3(0.30, 0.59, 0.11));
    vec3 desat = pow(mix(vec3(g), c, 0.45), vec3(1.25)) * 0.80;
    int bx = int(mod(px.x, 4.0));
    int by = int(mod(px.y, 4.0));
    float b = BAYER[by * 4 + bx] - 0.5;
    float levels = 5.0;
    vec3 q = clamp(floor(desat * levels + 0.5 + b * 0.85) / levels, 0.0, 1.0);
    q *= vec3(0.93, 1.0, 0.94);
    if (v_uv.y > u_reveal) {
        q = vec3(0.025, 0.09, 0.04);
    }
    o_color = vec4(q, 1.0);
}
