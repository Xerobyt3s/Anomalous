#version 460 core

in vec2 v_uv;
in float v_alpha;
in vec3 v_tint;

out vec4 o_color;

void main()
{
    float across = 1.0 - abs(v_uv.x * 2.0 - 1.0);
    across *= across;
    float head = smoothstep(0.0, 0.12, v_uv.y);
    float tail = smoothstep(1.0, 0.55, v_uv.y);
    float a = across * head * tail * v_alpha;
    o_color = vec4(clamp(v_tint, 0.0, 2.5), a);
}
