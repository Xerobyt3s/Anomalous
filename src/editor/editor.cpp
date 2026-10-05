#include "editor/editor.h"
#include "assets/mesh_data.h"
#include "carsys/items.h"
#include "core/arena.h"
#include "core/log.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "platform/input.h"
#include "platform/input_context.h"
#include "player/interact.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/gpu_mesh.h"
#include "ui/ui.h"
#include "vehicle/vehicle.h"
#include "world/terrain.h"
#include "world/zone.h"

namespace anom {
namespace {

Aabb entity_world_aabb(const Entity& e)
{
    const Mat4 model = mat4_trs(e.pos, e.rot, Vec3{e.scale, e.scale, e.scale});
    if (e.kind == EntityKind::Island && !e.mesh) {
        const f32 r = e.half.x;
        const Aabb local{Vec3{-r, -e.aux_value, -r}, Vec3{r, 2.0f, r}};
        return transform(mat4_trs(e.pos, e.rot, Vec3{1.0f, 1.0f, 1.0f}), local);
    }
    if (e.kind == EntityKind::IslandLink) {
        const Aabb local{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}};
        return transform(mat4_trs(e.pos, quat_identity(), Vec3{1.0f, 1.0f, 1.0f}), local);
    }
    if (e.mesh) {
        return transform(model, e.mesh->bounds);
    }
    const Aabb local{-e.half, e.half};
    return transform(mat4_trs(e.pos, e.rot, Vec3{1.0f, 1.0f, 1.0f}), local);
}

f32 pick_mesh_t(const Entity& e, Arena& scratch, Ray ray, f32 max_t)
{
    ArenaScope scope(scratch);

    const Quat inv = conjugate(e.rot);
    const f32 inv_scale = 1.0f / f_max(e.scale, 1e-4f);
    Ray local;
    local.origin = rotate(inv, ray.origin - e.pos) * inv_scale;
    local.dir = rotate(inv, ray.dir);

    FixedString<256> path;
    path.format("assets/meshes/%s.amsh", e.mesh_name.c_str());

    MeshData data;
    if (load_mesh(path.view(), scratch, data) != MeshParseError::Ok) {
        return -1.0f;
    }

    const f32 local_max = max_t * inv_scale;
    f32 best = -1.0f;
    for (u32 i = 0; i + 2 < data.indices.size(); i += 3) {
        Vec3 tri[3];
        for (u32 k = 0; k < 3; k++) {
            const AmshVertex& v = data.vertices[data.indices[i + k]];
            tri[k] = Vec3{v.pos[0], v.pos[1], v.pos[2]};
        }
        RayHitTri hit;
        if (ray_vs_triangle(local, tri[0], tri[1], tri[2], local_max, &hit)) {
            if (best < 0.0f || hit.t < best) {
                best = hit.t;
            }
        }
    }
    return best >= 0.0f ? best * e.scale : -1.0f;
}

const char* gizmo_mode_name(GizmoMode mode)
{
    if (mode == GizmoMode::Translate) {
        return "translate";
    }
    if (mode == GizmoMode::Rotate) {
        return "rotate";
    }
    return "scale";
}

void freeze_body(RigidBody& body, Vec3 pos, Quat rot)
{
    body.pos = pos;
    body.prev_pos = pos;
    body.rot = rot;
    body.prev_rot = rot;
    body.vel = Vec3{0.0f, 0.0f, 0.0f};
    body.angular_vel = Vec3{0.0f, 0.0f, 0.0f};
    body_wake(body);
}

} // namespace

bool operator==(const EntityRecord& a, const EntityRecord& b)
{
    return a.kind == b.kind && a.flags == b.flags && a.pos == b.pos && a.rot == b.rot
        && a.scale == b.scale && a.half == b.half && a.mesh_name == b.mesh_name
        && a.aux_kind == b.aux_kind && a.aux_value == b.aux_value && a.aux_data == b.aux_data
        && a.is_pickup == b.is_pickup && a.body_pos == b.body_pos && a.body_yaw == b.body_yaw;
}

void editor_record_entity(const World& world, const PhysWorld& phys, EntityHandle handle,
                          EntityRecord& out)
{
    out = EntityRecord{};
    const Entity* e = world.entity(handle);
    if (!e) {
        return;
    }
    out.kind = e->kind;
    out.flags = e->flags;
    out.pos = e->pos;
    out.rot = e->rot;
    out.scale = e->scale;
    out.half = e->half;
    out.mesh_name = e->mesh_name;
    out.aux_kind = e->aux_kind;
    out.aux_value = e->aux_value;
    out.aux_data = e->aux_data;
    out.is_pickup = e->kind == EntityKind::PartPickup;

    const RigidBody* body = phys.body(e->body);
    out.body_pos = body ? body->pos : e->pos;
    out.body_yaw = quat_yaw(body ? body->rot : e->rot);
}

EntityHandle editor_respawn(World& world, PhysWorld& phys, const EntityRecord& rec)
{
    if (rec.is_pickup) {
        Item item;
        item.kind = static_cast<ItemKind>(rec.aux_kind);
        item.condition = rec.aux_value;
        item.aux = static_cast<i32>(rec.aux_data);
        return interact_spawn_pickup(world, phys, item, rec.body_pos, rec.body_yaw,
                                     Vec3{0.0f, 0.0f, 0.0f});
    }

    const EntityHandle handle = world.spawn(rec.kind, rec.pos, rec.rot, rec.scale,
                                            rec.mesh_name.view(), rec.flags);
    if (Entity* e = world.entity(handle)) {
        e->half = rec.half;
        e->aux_kind = rec.aux_kind;
        e->aux_value = rec.aux_value;
        e->aux_data = rec.aux_data;
        e->mesh_name = rec.mesh_name;
    }
    return handle;
}

void editor_destroy_entity(World& world, PhysWorld& phys, EntityHandle handle)
{
    Entity* e = world.entity(handle);
    if (!e) {
        return;
    }
    if (e->body.valid()) {
        phys.body_destroy(e->body);
    }
    world.despawn(handle);
}

void Editor::init(Arena& storage)
{
    gizmo_.init();
    scene_.reset();
    brush_.init(storage);
    selection_ = EntityHandle{};
    grid_on_ = true;
    snapshot_ = storage.push_array<EntityRecord>(kMaxEntities);
    snapshot_count_ = 0;
}

void Editor::status(std::string_view message)
{
    status_.assign(message);
    status_time_ = 3.0f;
}

void Editor::select(const World& world, EntityHandle handle)
{
    selection_ = handle;
    if (const Entity* e = world.entity(handle)) {
        sel_yaw_ = quat_yaw(e->rot);
    }
}

void Editor::push_op(const EditorOp& op)
{
    op_count_ = op_cursor_;
    if (op_count_ >= kEditorMaxOps) {
        for (u32 i = 1; i < kEditorMaxOps; i++) {
            ops_[i - 1] = ops_[i];
        }
        op_count_--;
        op_cursor_--;
    }
    ops_[op_count_++] = op;
    op_cursor_ = op_count_;
}

void Editor::patch_handle(EntityHandle from, EntityHandle to)
{
    for (u32 i = 0; i < op_count_; i++) {
        if (ops_[i].handle == from) {
            ops_[i].handle = to;
        }
    }
    if (selection_ == from) {
        selection_ = to;
    }
}

void Editor::push_create(const World& world, const PhysWorld& phys, EntityHandle handle)
{
    EditorOp op;
    op.kind = EditorOpKind::Create;
    op.handle = handle;
    editor_record_entity(world, phys, handle, op.after);
    push_op(op);
    dirty_ = true;
}

void Editor::undo(World& world, PhysWorld& phys)
{
    if (op_cursor_ == 0) {
        status("nothing to undo");
        return;
    }
    EditorOp& op = ops_[--op_cursor_];
    if (op.kind == EditorOpKind::Transform) {
        if (Entity* e = world.entity(op.handle)) {
            e->pos = op.before.pos;
            e->rot = op.before.rot;
            e->scale = op.before.scale;
            e->half = op.before.half;
            e->aux_kind = op.before.aux_kind;
            e->aux_value = op.before.aux_value;
            e->aux_data = op.before.aux_data;
            e->mesh_name = op.before.mesh_name;
            if (RigidBody* body = phys.body(e->body)) {
                freeze_body(*body, op.before.body_pos,
                            quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, op.before.body_yaw));
            }
        }
    } else if (op.kind == EditorOpKind::Create) {
        editor_destroy_entity(world, phys, op.handle);
        if (selection_ == op.handle) {
            selection_ = EntityHandle{};
        }
    } else {
        const EntityHandle restored = editor_respawn(world, phys, op.before);
        patch_handle(op.handle, restored);
        op.handle = restored;
    }
    dirty_ = true;
    status("undo");
}

void Editor::redo(World& world, PhysWorld& phys)
{
    if (op_cursor_ >= op_count_) {
        status("nothing to redo");
        return;
    }
    EditorOp& op = ops_[op_cursor_++];
    if (op.kind == EditorOpKind::Transform) {
        if (Entity* e = world.entity(op.handle)) {
            e->pos = op.after.pos;
            e->rot = op.after.rot;
            e->scale = op.after.scale;
            e->half = op.after.half;
            e->aux_kind = op.after.aux_kind;
            e->aux_value = op.after.aux_value;
            e->aux_data = op.after.aux_data;
            e->mesh_name = op.after.mesh_name;
            if (RigidBody* body = phys.body(e->body)) {
                freeze_body(*body, op.after.body_pos,
                            quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, op.after.body_yaw));
            }
        }
    } else if (op.kind == EditorOpKind::Create) {
        const EntityHandle restored = editor_respawn(world, phys, op.after);
        patch_handle(op.handle, restored);
        op.handle = restored;
    } else {
        editor_destroy_entity(world, phys, op.handle);
        if (selection_ == op.handle) {
            selection_ = EntityHandle{};
        }
    }
    dirty_ = true;
    status("redo");
}

void Editor::snapshot_world(const World& world, const PhysWorld& phys)
{
    if (!snapshot_) {
        return;
    }
    snapshot_count_ = 0;
    const Pool<Entity>& pool = world.entities();
    for (u32 idx = 0; idx < pool.capacity() && snapshot_count_ < kMaxEntities; idx++) {
        if (!pool.at(idx)) {
            continue;
        }
        editor_record_entity(world, phys, pool.handle_at(idx), snapshot_[snapshot_count_++]);
    }
}

void Editor::restore_snapshot(World& world, PhysWorld& phys)
{
    if (!snapshot_) {
        return;
    }
    Pool<Entity>& pool = world.entities();
    for (u32 idx = 0; idx < pool.capacity(); idx++) {
        if (pool.at(idx)) {
            editor_destroy_entity(world, phys, pool.handle_at(idx));
        }
    }
    for (u32 i = 0; i < snapshot_count_; i++) {
        editor_respawn(world, phys, snapshot_[i]);
    }
}

void Editor::toggle(World& world, PhysWorld& phys)
{
    active_ = !active_;
    if (active_) {
        scene_.reset();
        restore_snapshot(world, phys);
        selection_ = EntityHandle{};
        op_count_ = 0;
        op_cursor_ = 0;
        drag_was_ = false;
    } else {
        snapshot_world(world, phys);
        gizmo_.init();
    }
}

EntityHandle Editor::pick(const Camera& cam, const World& world, Arena& scratch, Vec2 mouse,
                          Vec2 viewport) const
{
    const Ray ray = cam.mouse_ray(mouse, viewport);
    const Pool<Entity>& pool = world.entities();

    EntityHandle best;
    f32 best_t = 1e30f;
    for (u32 idx = 0; idx < pool.capacity(); idx++) {
        const Entity* e = pool.at(idx);
        if (!e || (!e->mesh && e->kind != EntityKind::Trigger && e->kind != EntityKind::Gravity
                   && e->kind != EntityKind::Island)) {
            continue;
        }
        if (e->kind == EntityKind::IslandLink && (e->aux_kind & 1u) == 0) {
            continue;
        }
        if (e->kind == EntityKind::Gravity && contains(entity_world_aabb(*e), ray.origin)) {
            continue;
        }
        f32 t = 0.0f;
        if (!ray_vs_aabb(ray, entity_world_aabb(*e), best_t, &t)) {
            continue;
        }
        if (e->mesh && !e->mesh_name.empty() && e->kind != EntityKind::Island) {
            const f32 exact = pick_mesh_t(*e, scratch, ray, best_t);
            if (exact < 0.0f) {
                continue;
            }
            t = exact;
        }
        if (t < best_t) {
            best_t = t;
            best = pool.handle_at(idx);
        }
    }
    return best;
}

void Editor::duplicate(World& world, PhysWorld& phys)
{
    if (!world.entity(selection_)) {
        return;
    }
    EntityRecord rec;
    editor_record_entity(world, phys, selection_, rec);
    rec.pos += Vec3{2.0f, 0.0f, 2.0f};
    rec.body_pos += Vec3{2.0f, 0.0f, 2.0f};

    const EntityHandle copy = editor_respawn(world, phys, rec);
    select(world, copy);
    push_create(world, phys, copy);
    status("duplicated entity");
}

void Editor::delete_selection(World& world, PhysWorld& phys)
{
    if (!world.entity(selection_)) {
        return;
    }
    EditorOp op;
    op.kind = EditorOpKind::Delete;
    op.handle = selection_;
    editor_record_entity(world, phys, selection_, op.before);
    editor_destroy_entity(world, phys, selection_);
    push_op(op);
    selection_ = EntityHandle{};
    dirty_ = true;
    status("deleted entity");
}

void Editor::drive_gizmo(const Input& input, const Camera& cam, World& world, PhysWorld& phys,
                         Vec2 viewport, bool over_panel)
{
    Entity* sel = world.entity(selection_);
    if (!sel) {
        drag_was_ = false;
        return;
    }

    RigidBody* sel_body = phys.body(sel->body);
    const bool no_scale = sel_body != nullptr || sel->kind == EntityKind::Trigger
                       || sel->kind == EntityKind::Gravity || sel->kind == EntityKind::Island
                       || sel->kind == EntityKind::IslandLink;

    Vec3 gizmo_pos = sel_body ? sel_body->pos : sel->pos;
    const Vec3 prev_pos = gizmo_pos;
    const f32 prev_yaw = sel_yaw_;
    const f32 prev_scale = sel->scale;

    const bool drag_was = gizmo_.dragging();
    if (!drag_was) {
        gizmo_.set_basis(quat_identity());
    }
    if (!over_panel || drag_was) {
        GizmoTarget target;
        target.pos = &gizmo_pos;
        target.yaw = &sel_yaw_;
        target.scale = no_scale ? nullptr : &sel->scale;
        gizmo_.update(input, cam, viewport, target, snap_);
    }
    if (gizmo_.dragging() && !drag_was) {
        editor_record_entity(world, phys, selection_, drag_before_);
        drag_was_ = true;
    }

    const bool moved = gizmo_pos != prev_pos;
    const bool turned = sel_yaw_ != prev_yaw;
    if (sel_body) {
        if (moved || turned) {
            const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, sel_yaw_);
            freeze_body(*sel_body, gizmo_pos, rot);
            if (sel->kind == EntityKind::PartPickup) {
                const ItemKind kind = static_cast<ItemKind>(sel->aux_kind);
                sel->rot = rot * item_cargo_rot(kind);
                sel->pos = gizmo_pos - rotate(sel->rot, item_mesh_center(kind));
            }
        }
    } else {
        if (moved) {
            sel->pos = gizmo_pos;
        }
        if (turned) {
            sel->rot = normalize(quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, sel_yaw_ - prev_yaw)
                                 * sel->rot);
        }
    }
    if (moved || turned || sel->scale != prev_scale) {
        dirty_ = true;
    }

    if (!gizmo_.dragging() && drag_was_) {
        drag_was_ = false;
        EntityRecord after;
        editor_record_entity(world, phys, selection_, after);
        if (after != drag_before_) {
            EditorOp op;
            op.kind = EditorOpKind::Transform;
            op.handle = selection_;
            op.before = drag_before_;
            op.after = after;
            push_op(op);
        }
    }
}

void Editor::update(const Input& input, const InputContext& ctx, Camera& cam, World& world,
                    PhysWorld& phys, Terrain& terrain, Arena& arena, Arena& scratch,
                    Vehicle* veh, InteractBoxes* boxes, Vec2 viewport,
                    std::string_view zone_dir, f32 dt)
{
    if (!active_) {
        return;
    }
    if (status_time_ > 0.0f) {
        status_time_ -= dt;
    }

    const bool keys = ctx.keyboard(InputLayer::Editor);
    const bool over_panel = !ctx.pointer(InputLayer::Editor);

    if (keys && !brush_mode_) {
        cam.fly_update(input, dt);

        if (input.pressed(Key::Num1)) {
            gizmo_.set_mode(GizmoMode::Translate);
        }
        if (input.pressed(Key::Num2)) {
            gizmo_.set_mode(GizmoMode::Rotate);
        }
        if (input.pressed(Key::Num3)) {
            gizmo_.set_mode(GizmoMode::Scale);
        }
        if (input.pressed(Key::G)) {
            grid_on_ = !grid_on_;
        }
        if (input.down(Key::LeftControl) && input.pressed(Key::Z)) {
            undo(world, phys);
        }
        if (input.down(Key::LeftControl) && input.pressed(Key::Y)) {
            redo(world, phys);
        }
    }

    if (brush_mode_) {
        brush_.update(*this, input, ctx, scratch, viewport);
        return;
    }

    const bool scene_consumed = scene_.update(*this, input, cam, world, phys, terrain, veh,
                                              boxes, viewport, over_panel);
    if (!scene_consumed) {
        drive_gizmo(input, cam, world, phys, viewport, over_panel);
    }

    if (!scene_consumed && !gizmo_.dragging() && !over_panel && input.pressed(MouseButton::Left)
        && !input.down(MouseButton::Right)) {
        const EntityHandle hit = pick(cam, world, scratch, input.mouse_pos(), viewport);
        if (hit.valid()) {
            select(world, hit);
        } else {
            selection_ = EntityHandle{};
        }
    }

    if (keys) {
        if (input.pressed(Key::Delete)) {
            delete_selection(world, phys);
        }
        if (input.down(Key::LeftControl) && input.pressed(Key::D)) {
            duplicate(world, phys);
        }
        if (input.down(Key::LeftControl) && input.pressed(Key::S)) {
            want_save_ = true;
        }
    }

    if (want_save_) {
        want_save_ = false;
        if (zone_save(zone_dir, scratch, world, phys, terrain)) {
            dirty_ = false;
            status("saved zone.cfg");
        } else {
            status("save failed");
        }
    }
    if (want_reload_) {
        want_reload_ = false;
        selection_ = EntityHandle{};
        op_count_ = 0;
        op_cursor_ = 0;

        ZonePickups pickups;
        if (zone_reload(zone_dir, arena, scratch, world, phys, terrain, &pickups)) {
            interact_spawn_zone_pickups(world, phys, terrain, pickups);
            snapshot_world(world, phys);
            dirty_ = false;
            status("reloaded zone.cfg");
        } else {
            status("reload failed");
        }
    }
}

void Editor::render(Ui& ui, DebugDraw& debug, TextRenderer& text, const Input& input,
                    Arena& scratch, const Camera& cam, World& world, PhysWorld& phys,
                    const Terrain& terrain, Vehicle* veh, InteractBoxes* boxes,
                    const TapeLibrary* tapes, Vec2 viewport)
{
    if (!active_) {
        return;
    }
    if (brush_mode_) {
        brush_.render(*this, ui, debug, text, input, scratch, viewport);
        return;
    }

    if (grid_on_) {
        const f32 step = snap_.pos > 0.0f ? snap_.pos : 1.0f;
        const f32 gx = std::floor(cam.pos.x / step + 0.5f) * step;
        const f32 gz = std::floor(cam.pos.z / step + 0.5f) * step;
        const f32 gy = terrain.heightfield().sample(gx, gz);
        debug.grid(Vec3{gx, gy, gz}, 40.0f, step, dd_rgba(70, 80, 95, 120));
    }

    const Pool<Entity>& pool = world.entities();
    const auto island_at = [&pool](std::string_view name) -> const Entity* {
        for (u32 idx : pool.live_indices()) {
            const Entity* e = pool.at(idx);
            if (e && e->kind == EntityKind::Island && e->mesh_name == name) {
                return e;
            }
        }
        return nullptr;
    };
    for (u32 idx = 0; idx < pool.capacity(); idx++) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::Island) {
            const bool is_sel = pool.handle_at(idx) == selection_;
            const Vec3 up = rotate(e->rot, Vec3{0.0f, 1.0f, 0.0f});
            debug.arrow(e->pos, e->pos + up * 4.0f, 0.6f, is_sel ? kDdYellow : kDdGreen);
            debug.text_3d(e->pos + up * 4.5f, 13.0f, is_sel ? kDdYellow : kDdGreen, "%s", e->mesh_name.c_str());
            if (is_sel) {
                debug.obb(e->pos, e->rot, Vec3{e->half.x, 0.2f, e->half.x}, kDdYellow);
            }
            continue;
        }
        if (e && e->kind == EntityKind::IslandLink) {
            const std::string_view spec = e->mesh_name.view();
            const size_t split = spec.find('>');
            const Entity* from = split == std::string_view::npos ? nullptr : island_at(spec.substr(0, split));
            if (!from) {
                continue;
            }
            const bool is_sel = pool.handle_at(idx) == selection_;
            const std::string_view to_name = spec.substr(split + 1);
            const Entity* to = to_name == "ground" ? nullptr : island_at(to_name);
            Vec3 end = to ? to->pos : from->pos;
            if (!to && (e->aux_kind & 1u) != 0) {
                end = e->pos;
                debug.cross(end, 1.5f, is_sel ? kDdYellow : kDdOrange);
            } else if (!to) {
                end = Vec3{from->pos.x, from->pos.y - 12.0f, from->pos.z};
            }
            debug.line(from->pos, end, is_sel ? kDdYellow : kDdOrange);
            continue;
        }
        if (e && e->kind == EntityKind::Gravity) {
            const bool is_sel = pool.handle_at(idx) == selection_;
            debug.obb(e->pos, e->rot, e->half, is_sel ? kDdYellow : kDdMagenta);
            if (is_sel && e->scale > 0.0f) {
                debug.obb(e->pos, e->rot, e->half + Vec3{e->scale, e->scale, e->scale}, kDdDark);
            }
            const Vec3 down = rotate(e->rot, Vec3{0.0f, -1.0f, 0.0f});
            const f32 reach = f_min(e->half.y, 4.0f);
            debug.arrow(e->pos, e->pos + down * reach, 0.5f, kDdMagenta);
            debug.text_3d(e->pos + rotate(e->rot, Vec3{0.0f, e->half.y + 0.3f, 0.0f}), 13.0f,
                          kDdMagenta, "%s", e->mesh_name.c_str());
            continue;
        }
        if (!e || e->kind != EntityKind::Trigger) {
            continue;
        }
        const bool is_sel = pool.handle_at(idx) == selection_;
        debug.obb(e->pos, e->rot, e->half, is_sel ? kDdYellow : kDdCyan);
        debug.text_3d(e->pos + Vec3{0.0f, e->half.y + 0.3f, 0.0f}, 13.0f, kDdCyan, "%s",
                      e->mesh_name.c_str());
    }

    Entity* sel = world.entity(selection_);
    if (sel) {
        const RigidBody* sel_body = phys.body(sel->body);
        if (sel->mesh) {
            const Aabb local = sel->mesh->bounds;
            const Vec3 center_local = (local.min + local.max) * 0.5f;
            const Vec3 half = (local.max - local.min) * (0.5f * sel->scale);
            const Vec3 center = sel->pos + rotate(sel->rot, center_local * sel->scale);
            debug.obb(center, sel->rot, half, kDdYellow);
        }
        gizmo_.render(debug, cam, sel_body ? sel_body->pos : sel->pos);
    }

    ui.panel_begin("editor [f8]", 16.0f, 16.0f, 260.0f);
    ui.label("mode: %s  (1/2/3)", gizmo_mode_name(gizmo_.mode()));
    ui.checkbox("grid (g)", grid_on_);
    ui.slider("snap pos", snap_.pos, 0.0f, 5.0f);
    ui.slider("snap deg", snap_.angle_deg, 0.0f, 90.0f);
    ui.slider("snap scale", snap_.scale, 0.0f, 1.0f);

    if (sel) {
        ui.label("sel: %s", sel->mesh_name.empty() ? "(trigger)" : sel->mesh_name.c_str());
        ui.label("pos %.2f %.2f %.2f", static_cast<f64>(sel->pos.x),
                 static_cast<f64>(sel->pos.y), static_cast<f64>(sel->pos.z));
        ui.label("yaw %.1f  scale %.2f", static_cast<f64>(sel_yaw_ * kRadToDeg),
                 static_cast<f64>(sel->scale));
        if (ui.button("duplicate (ctrl+d)")) {
            duplicate(world, phys);
        }
        if (ui.button("delete (del)")) {
            delete_selection(world, phys);
        }
    } else {
        ui.label("click an entity to select");
    }

    ui.label("undo %u | redo %u", op_cursor_, op_count_ - op_cursor_);
    if (ui.button("undo (ctrl+z)")) {
        undo(world, phys);
    }
    if (ui.button("redo (ctrl+y)")) {
        redo(world, phys);
    }

    ui.label("entities: %u", world.count());
    ui.label("%s", dirty_ ? "unsaved changes *" : "no changes");
    if (ui.button("save zone (ctrl+s)")) {
        want_save_ = true;
    }
    if (ui.button("reload zone")) {
        want_reload_ = true;
    }
    if (ui.button("structure editor")) {
        brush_mode_ = true;
        brush_.reset(scratch);
        selection_ = EntityHandle{};
        scene_.set_box_selection(-1);
        gizmo_.init();
    }
    if (status_time_ > 0.0f) {
        ui.label("%s", status_.c_str());
    }
    ui.panel_end();

    scene_.render(*this, ui, debug, cam, world, phys, terrain, veh, boxes, tapes, viewport);
}

} // namespace anom
