#version 450 core

in vec3 v_color;
in float v_alpha;

out vec4 o_color;

void main()
{
    if (v_alpha < 0.01) {
        discard;
    }
    o_color = vec4(v_color, 1.0);
}
