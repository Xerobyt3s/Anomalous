#pragma once

#include "core/types.h"
#include "core/pool.h"
#include "math/vmath.h"
#include "world/world.h"
#include "editor/gizmo.h"

struct GameInput;
struct Camera;
struct World;
struct PhysWorld;
struct Terrain;
struct Vehicle;

#define EDITOR_MAX_OPS 128

typedef struct EntityRecord {
    EntityKind kind;
    u32 flags;
    Vec3 pos;
    Quat rot;
    f32 scale;
    Vec3 half;
    char mesh_name[32];
    u32 aux_kind;
    f32 aux_value;
    u32 aux_data;
    b32 is_pickup;
    Vec3 body_pos;
    f32 body_yaw;
} EntityRecord;

typedef enum EditorOpKind {
    EDITOR_OP_TRANSFORM,
    EDITOR_OP_CREATE,
    EDITOR_OP_DELETE,
} EditorOpKind;

typedef struct EditorOp {
    EditorOpKind kind;
    Handle handle;
    EntityRecord before;
    EntityRecord after;
} EditorOp;

typedef struct EditorState {
    b32 active;
    Gizmo gizmo;
    Handle selection;
    f32 sel_yaw;
    b32 grid_on;
    f32 snap_pos;
    f32 snap_ang;
    f32 snap_scale;
    b32 dirty;
    b32 want_save;
    b32 want_reload;
    char status[96];
    f32 status_time;
    EditorOp ops[EDITOR_MAX_OPS];
    u32 op_count;
    u32 op_cursor;
    b32 drag_was;
    EntityRecord drag_before;
    b32 car_boxes_on;
    i32 box_sel;
    b32 brush_mode;
    i32 brush_sel;
} EditorState;

void editor_init(EditorState* ed);
void editor_toggle(EditorState* ed, struct World* world, struct PhysWorld* phys);
void editor_snapshot_world(struct World* world, struct PhysWorld* phys);
void editor_update(EditorState* ed, const struct GameInput* input, struct Camera* cam,
                   struct World* world, struct PhysWorld* phys, struct Terrain* terrain,
                   struct Vehicle* veh, const char* zone_dir, f32 dt);
void editor_render(EditorState* ed, const struct GameInput* input, const struct Camera* cam,
                   struct World* world, struct PhysWorld* phys, struct Terrain* terrain,
                   struct Vehicle* veh);

void editor_record_entity(struct World* world, struct PhysWorld* phys, Handle handle,
                          EntityRecord* out);
void editor_push_create(EditorState* ed, struct World* world, struct PhysWorld* phys,
                        Handle handle);
void editor_status_msg(EditorState* ed, const char* msg);
void editor_select_entity(EditorState* ed, struct World* world, Handle handle);

void editor_scene_reset(void);
b32  editor_scene_update(EditorState* ed, const struct GameInput* input, const struct Camera* cam,
                         struct World* world, struct PhysWorld* phys,
                         const struct Terrain* terrain, struct Vehicle* veh);
void editor_scene_render(EditorState* ed, const struct GameInput* input,
                         const struct Camera* cam, struct World* world, struct PhysWorld* phys,
                         const struct Terrain* terrain, struct Vehicle* veh);
