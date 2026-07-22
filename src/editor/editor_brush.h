#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct EditorState;
struct GameInput;
struct Camera;
struct Terrain;

#define STRUCT_MAX_BRUSHES 128
#define STRUCT_NAME_MAX 32
#define BRUSH_POLY_MAX 8

typedef enum BrushKind {
    BRUSH_BOX,
    BRUSH_WEDGE,
    BRUSH_POLY,
} BrushKind;

typedef struct Brush {
    BrushKind kind;
    b32 subtract;
    Vec3 pos;
    Vec3 size;
    f32 yaw;
    f32 hollow;
    Vec2 points[BRUSH_POLY_MAX];
    u32 point_count;
    char material[32];
} Brush;

typedef struct Structure {
    char name[STRUCT_NAME_MAX];
    Brush brushes[STRUCT_MAX_BRUSHES];
    u32 count;
} Structure;

void editor_brush_reset(void);
void editor_brush_update(struct EditorState* ed, const struct GameInput* input,
                         const struct Camera* cam, const struct Terrain* terrain);
void editor_brush_render(struct EditorState* ed, const struct GameInput* input,
                         const struct Camera* cam, const struct Terrain* terrain);
