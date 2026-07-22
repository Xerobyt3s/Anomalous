#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "render/camera.h"

struct GameInput;

typedef enum GizmoMode {
    GIZMO_TRANSLATE,
    GIZMO_ROTATE,
    GIZMO_SCALE,
} GizmoMode;

typedef struct Gizmo {
    GizmoMode mode;
    Quat basis;
    i32 active_axis;
    b32 dragging;
    f32 drag_start_param;
    f32 drag_delta;
    Vec3 start_pos;
    f32 start_yaw;
    f32 start_scale;
} Gizmo;

void gizmo_init(Gizmo* gizmo);
b32  gizmo_update(Gizmo* gizmo, const struct GameInput* input, const Camera* cam, Vec2 viewport,
                  Vec3* pos, f32* yaw, f32* scale, f32 snap_pos, f32 snap_ang, f32 snap_scale);
void gizmo_render(const Gizmo* gizmo, const Camera* cam, Vec3 pos);
