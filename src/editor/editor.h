#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "editor/editor_brush.h"
#include "editor/editor_scene.h"
#include "editor/gizmo.h"
#include "math/vmath.h"
#include "world/entity.h"

#include <string_view>

namespace anom {
class EntityMeshes;
struct GpuMesh;

class Arena;
struct Camera;
class DebugDraw;
class Input;
class InputContext;
class PhysWorld;
class Terrain;
class TapeLibrary;
class TextRenderer;
class Ui;
class Vehicle;
class InteractBoxes;

inline constexpr u32 kEditorMaxOps = 128;

struct EntityRecord {
    EntityKind kind = EntityKind::StaticMesh;
    u32 flags = 0;
    Vec3 pos;
    Quat rot = quat_identity();
    f32 scale = 1.0f;
    Vec3 half;
    FixedString<32> mesh_name;
    u32 aux_kind = 0;
    f32 aux_value = 0.0f;
    u32 aux_data = 0;
    bool is_pickup = false;
    Vec3 body_pos;
    f32 body_yaw = 0.0f;
};

bool operator==(const EntityRecord& a, const EntityRecord& b);
inline bool operator!=(const EntityRecord& a, const EntityRecord& b) { return !(a == b); }

enum class EditorOpKind : u32 {
    Transform,
    Create,
    Delete,
};

struct EditorOp {
    EditorOpKind kind = EditorOpKind::Transform;
    EntityHandle handle;
    EntityRecord before;
    EntityRecord after;
};

void editor_record_entity(const World& world, const PhysWorld& phys, EntityHandle handle,
                          EntityRecord& out);
EntityHandle editor_respawn(World& world, PhysWorld& phys, const EntityRecord& rec);
void editor_destroy_entity(World& world, PhysWorld& phys, EntityHandle handle);

class Editor {
public:
    void set_meshes(const EntityMeshes* meshes) { meshes_ = meshes; }
    void init(Arena& storage);

    void toggle(World& world, PhysWorld& phys);
    bool active() const { return active_; }

    void snapshot_world(const World& world, const PhysWorld& phys);
    void restore_snapshot(World& world, PhysWorld& phys);
    u32 snapshot_count() const { return snapshot_count_; }

    void update(const Input& input, const InputContext& ctx, Camera& cam, World& world,
                PhysWorld& phys, Terrain& terrain, Arena& arena, Arena& scratch, Vehicle* veh,
                InteractBoxes* boxes, Vec2 viewport, std::string_view zone_dir, f32 dt);
    void render(Ui& ui, DebugDraw& debug, TextRenderer& text, const Input& input,
                Arena& scratch, const Camera& cam, World& world, PhysWorld& phys,
                const Terrain& terrain, Vehicle* veh, InteractBoxes* boxes,
                const TapeLibrary* tapes, Vec2 viewport);

    void select(const World& world, EntityHandle handle);
    void clear_selection() { selection_ = EntityHandle{}; }
    EntityHandle selection() const { return selection_; }
    void duplicate(World& world, PhysWorld& phys);
    void delete_selection(World& world, PhysWorld& phys);

    void push_create(const World& world, const PhysWorld& phys, EntityHandle handle);
    void undo(World& world, PhysWorld& phys);
    void redo(World& world, PhysWorld& phys);
    u32 op_count() const { return op_count_; }
    u32 op_cursor() const { return op_cursor_; }

    EntityHandle pick(const Camera& cam, const World& world, Arena& scratch, Vec2 mouse,
                      Vec2 viewport) const;

    void status(std::string_view message);
    std::string_view status_text() const { return status_.view(); }
    f32 status_time() const { return status_time_; }

    bool dirty() const { return dirty_; }
    void mark_dirty() { dirty_ = true; }
    void request_save() { want_save_ = true; }
    void request_reload() { want_reload_ = true; }

    BrushEditor& brush() { return brush_; }

    EditorScene& scene() { return scene_; }
    const EditorScene& scene() const { return scene_; }

    Gizmo& gizmo() { return gizmo_; }
    const Gizmo& gizmo() const { return gizmo_; }
    GizmoSnap& snap() { return snap_; }
    const GizmoSnap& snap() const { return snap_; }
    bool grid_on() const { return grid_on_; }
    bool brush_mode() const { return brush_mode_; }
    void set_brush_mode(bool on) { brush_mode_ = on; }

private:
    const GpuMesh* mesh_of(u32 idx) const;
    const EntityMeshes* meshes_ = nullptr;
    void push_op(const EditorOp& op);
    void patch_handle(EntityHandle from, EntityHandle to);
    void drive_gizmo(const Input& input, const Camera& cam, World& world, PhysWorld& phys,
                     Vec2 viewport, bool over_panel);

    bool active_ = false;
    Gizmo gizmo_;
    EditorScene scene_;
    BrushEditor brush_;
    GizmoSnap snap_;
    EntityHandle selection_;
    f32 sel_yaw_ = 0.0f;
    bool grid_on_ = true;
    bool dirty_ = false;
    bool want_save_ = false;
    bool want_reload_ = false;
    bool brush_mode_ = false;
    bool drag_was_ = false;
    EntityRecord drag_before_;

    FixedString<96> status_;
    f32 status_time_ = 0.0f;

    EditorOp ops_[kEditorMaxOps];
    u32 op_count_ = 0;
    u32 op_cursor_ = 0;

    EntityRecord* snapshot_ = nullptr;
    u32 snapshot_count_ = 0;
};

}
