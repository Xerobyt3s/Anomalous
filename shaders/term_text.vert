#version 450 core

layout(location = 0) in vec2 i_pos;
layout(location = 1) in float i_glyph;
layout(location = 2) in float i_color;
layout(location = 3) in float i_width;

out vec2 v_uv;
flat out int v_color;

const vec2 SCREEN = vec2(640.0, 400.0);
const float SLOT_W = 16.0;
const float SLOT_H = 12.0;
const float ATLAS_COLS = 32.0;
const float ATLAS_ROWS = 42.0;

void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 px = i_pos + corner * vec2(i_width, SLOT_H);
    vec2 ndc = px / SCREEN * 2.0 - 1.0;

    vec2 slot = vec2(mod(i_glyph, ATLAS_COLS), floor(i_glyph / ATLAS_COLS));
    vec2 uv_px = slot * vec2(SLOT_W, SLOT_H) + corner * vec2(i_width, SLOT_H);
    v_uv = uv_px / vec2(ATLAS_COLS * SLOT_W, ATLAS_ROWS * SLOT_H);
    v_color = int(i_color);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
