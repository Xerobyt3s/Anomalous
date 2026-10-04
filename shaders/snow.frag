#version 460 core

in vec2 v_local;
in float v_alpha;
in vec3 v_tint;

out vec4 o_color;

void main()
{
    float across = 1.0 - v_local.x * v_local.x;
    float along = 1.0 - (v_local.y * 2.0 - 1.0) * (v_local.y * 2.0 - 1.0);
    float shape = across * along;
    if (shape <= 0.0) {
        discard;
    }
    o_color = vec4(clamp(v_tint, 0.0, 2.5), shape * v_alpha);
}
