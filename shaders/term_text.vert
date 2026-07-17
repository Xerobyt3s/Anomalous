#version 450 core

layout(location = 0) in vec2 i_pos;
layout(location = 1) in float i_glyph;
layout(location = 2) in float i_color;

out vec2 v_uv;
flat out int v_color;

const vec2 SCREEN = vec2(640.0, 400.0);
const float CELL = 8.0;
const float ATLAS_COLS = 16.0;
const float ATLAS_ROWS = 6.0;

void main()
{
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vec2 px = i_pos + corner * CELL;
    vec2 ndc = px / SCREEN * 2.0 - 1.0;

    vec2 cell = vec2(mod(i_glyph, ATLAS_COLS), floor(i_glyph / ATLAS_COLS));
    v_uv = (cell + corner) / vec2(ATLAS_COLS, ATLAS_ROWS);
    v_color = int(i_color);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
