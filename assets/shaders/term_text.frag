#version 450 core

layout(binding = 0) uniform sampler2D u_atlas;

in vec2 v_uv;
flat in int v_color;

out vec4 o_color;

const vec3 PALETTE[6] = vec3[6](
    vec3(0.027, 0.086, 0.035),
    vec3(0.243, 0.706, 0.322),
    vec3(0.549, 1.000, 0.588),
    vec3(0.863, 0.667, 0.235),
    vec3(0.922, 0.314, 0.235),
    vec3(0.133, 0.376, 0.180));

void main()
{
    float alpha = texture(u_atlas, v_uv).r;
    o_color = vec4(PALETTE[clamp(v_color, 0, 5)], alpha);
}
