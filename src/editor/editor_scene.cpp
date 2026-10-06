#include "editor/editor_scene.h"
#include "audio/tapes.h"
#include "editor/editor.h"
#include "physics/gravity_field.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "platform/input.h"
#include "player/interact.h"
#include "render/camera.h"
#include "world/zone.h"
#include "render/debug_draw.h"
#include "terminal/disks.h"
#include "ui/ui.h"
#include "vehicle/vehicle.h"
#include "world/islands/island_field.h"
#include "world/islands/sdf_noise.h"
#include "world/terrain.h"

#include <cstdio>

namespace anom {
namespace {

constexpr f32 kPlaceRange = 500.0f;
constexpr f32 kTriggerDistance = 8.0f;
constexpr f32 kGravityDistance = 14.0f;
constexpr f32 kGhostSpawnDistance = 10.0f;
constexpr f32 kGhostSpawnLift = 1.5f;
constexpr f32 kIslandDistance = 40.0f;
constexpr f32 kIslandLift = 14.0f;
constexpr f32 kIslandRadius = 10.0f;
constexpr f32 kIslandDepth = 8.0f;
constexpr u32 kIslandNameTries = 1000;

bool island_named(const World& world, std::string_view name)
{
    for (u32 idx : world.entities().live_indices()) {
        const Entity* e = world.entities().at(idx);
        if (e && e->kind == EntityKind::Island && e->mesh_name == name) {
            return true;
        }
    }
    return false;
}

bool link_exists(const World& world, std::string_view spec)
{
    for (u32 idx : world.entities().live_indices()) {
        const Entity* e = world.entities().at(idx);
        if (e && e->kind == EntityKind::IslandLink && e->mesh_name == spec) {
            return true;
        }
    }
    return false;
}

bool ends_with(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

EntityKind entity_kind_of(SpawnKind kind)
{
    switch (kind) {
    case SpawnKind::Tree:
        return EntityKind::Tree;
    case SpawnKind::Building:
        return EntityKind::Building;
    default:
        return EntityKind::StaticMesh;
    }
}

Ray to_body_local(const RigidBody& body, Ray world_ray)
{
    const Quat inv = conjugate(body.rot);
    Ray local;
    local.origin = rotate(inv, world_ray.origin - body.pos);
    local.dir = rotate(inv, world_ray.dir);
    return local;
}

} // namespace

void EditorScene::reset()
{
    fs::DirEntry entries[kSceneMaxMeshes];
    const u32 found = fs::list_dir("assets/meshes", entries);

    mesh_count_ = 0;
    for (u32 i = 0; i < found && mesh_count_ < kSceneMaxMeshes; i++) {
        if (entries[i].is_dir) {
            continue;
        }
        const std::string_view name = entries[i].name.view();
        if (!ends_with(name, ".amsh") || ends_with(name, "_col.amsh")) {
            continue;
        }
        meshes_[mesh_count_++].assign(name.substr(0, name.size() - 5));
    }

    place_mesh_ = -1;
    place_item_ = -1;
    spawn_kind_ = SpawnKind::Static;
    box_sel_ = -1;
    preview_valid_ = false;
}

void EditorScene::set_place_mesh(i32 index)
{
    place_mesh_ = index == place_mesh_ ? -1 : index;
    place_item_ = -1;
}

void EditorScene::set_place_item(i32 kind)
{
    place_item_ = kind == place_item_ ? -1 : kind;
    place_mesh_ = -1;
}

void EditorScene::clear_placement()
{
    place_mesh_ = -1;
    place_item_ = -1;
}

void EditorScene::cycle_spawn_kind()
{
    spawn_kind_ = static_cast<SpawnKind>((static_cast<u32>(spawn_kind_) + 1) % 3);
}

EntityHandle EditorScene::add_trigger(Editor& editor, const Camera& cam, World& world,
                                      PhysWorld& phys, const Terrain& terrain)
{
    Vec3 pos = cam.pos + cam.forward() * kTriggerDistance;
    pos.y = terrain.heightfield().sample(pos.x, pos.z) + 1.0f;

    const EntityHandle handle = world.spawn(EntityKind::Trigger, pos, quat_identity(), 1.0f, "",
                                            kEntityFlagInteractable);
    if (Entity* e = world.entity(handle)) {
        e->mesh_name.assign("trigger");
        e->half = Vec3{1.0f, 1.0f, 1.0f};
    }
    editor.select(world, handle);
    editor.push_create(world, phys, handle);
    return handle;
}

EntityHandle EditorScene::add_gravity(Editor& editor, const Camera& cam, World& world,
                                      PhysWorld& phys)
{
    const Vec3 pos = cam.pos + cam.forward() * kGravityDistance;
    const EntityHandle handle = world.spawn(EntityKind::Gravity, pos, quat_identity(), 3.0f, "", 0);
    if (Entity* e = world.entity(handle)) {
        e->mesh_name.assign("gravity");
        e->half = Vec3{6.0f, 4.0f, 6.0f};
        e->aux_value = kDefaultGravity;
        e->aux_data = 90;
    }
    editor.select(world, handle);
    editor.push_create(world, phys, handle);
    return handle;
}

EntityHandle EditorScene::add_ghost_spawn(Editor& editor, const Camera& cam, World& world, PhysWorld& phys,
                                          const Terrain& terrain)
{
    Vec3 pos = cam.pos + cam.forward() * kGhostSpawnDistance;
    pos.y = terrain.heightfield().sample(pos.x, pos.z) + kGhostSpawnLift;
    const EntityHandle handle = world.spawn(EntityKind::GhostSpawn, pos, quat_identity(), 1.0f, "", 0);
    if (Entity* e = world.entity(handle)) {
        e->mesh_name.assign(ghost_types_.empty() ? std::string_view("wisp") : std::string_view(ghost_types_.front()));
        e->half = kGhostSpawnHalf;
        e->aux_kind = 1;
    }
    editor.select(world, handle);
    editor.push_create(world, phys, handle);
    return handle;
}

EntityHandle EditorScene::add_scene_thing(Editor& editor, const Camera& cam, World& world, PhysWorld& phys,
                                          const Terrain& terrain, EntityKind kind)
{
    Vec3 pos = cam.pos + cam.forward() * kGhostSpawnDistance;
    pos.y = terrain.heightfield().sample(pos.x, pos.z);
    u32 order = 0;
    for (u32 idx : world.entities().live_indices()) {
        const Entity* e = world.entities().at(idx);
        order += e && e->kind == EntityKind::SpawnPoint ? 1u : 0u;
    }
    const EntityHandle handle = world.spawn(kind, pos, quat_identity(), 1.0f, "", 0);
    if (Entity* e = world.entity(handle)) {
        if (kind == EntityKind::Prop) {
            e->half = Vec3{0.5f, 0.5f, 0.5f};
            e->pos.y += e->half.y;
            e->aux_data = 0x6B6966u;
            e->aux_kind = surface_from_name("concrete");
        } else if (kind == EntityKind::Bench) {
            e->half = kBenchHalf;
            e->pos.y += kBenchHalf.y * 2.0f;
        } else {
            e->half = kSpawnPointHalf;
            e->aux_kind = order;
        }
    }
    editor.select(world, handle);
    editor.push_create(world, phys, handle);
    return handle;
}

EntityHandle EditorScene::add_island(Editor& editor, const Camera& cam, World& world,
                                     PhysWorld& phys, const Terrain& terrain)
{
    FixedString<32> name;
    for (u32 i = 1; i < kIslandNameTries; i++) {
        name.format("isle_%u", i);
        if (!island_named(world, name.view())) {
            break;
        }
    }
    Vec3 flat = cam.forward();
    flat.y = 0.0f;
    flat = length_sq(flat) > 1e-6f ? normalize(flat) : Vec3{0.0f, 0.0f, -1.0f};
    Vec3 pos = cam.pos + flat * kIslandDistance;
    pos.y = terrain.heightfield().sample(pos.x, pos.z) + kIslandLift;
    const EntityHandle handle = world.spawn(EntityKind::Island, pos, quat_identity(), 1.0f, name.view(), 0);
    if (Entity* e = world.entity(handle)) {
        e->half = Vec3{kIslandRadius, 0.0f, 0.0f};
        e->aux_value = kIslandDepth;
        e->aux_data = seed_from_name(name.view(), 0u) % 1000u;
    }
    editor.select(world, handle);
    editor.push_create(world, phys, handle);
    return handle;
}

EntityHandle EditorScene::add_link(Editor& editor, World& world, PhysWorld& phys,
                                   std::string_view from, std::string_view to)
{
    FixedString<64> spec;
    spec.format("%.*s>%.*s", static_cast<int>(from.size()), from.data(), static_cast<int>(to.size()),
                to.data());
    if (from == to || (to != "ground" && !island_named(world, to))) {
        editor.status("no island named that");
        return EntityHandle{};
    }
    if (link_exists(world, spec.view())) {
        editor.status("already linked");
        return EntityHandle{};
    }
    const EntityHandle handle = world.spawn(EntityKind::IslandLink, Vec3{}, quat_identity(), 1.0f, spec.view(), 0);
    if (Entity* e = world.entity(handle)) {
        e->aux_value = kLinkDefaultWidth;
    }
    editor.select(world, handle);
    editor.push_create(world, phys, handle);
    return handle;
}

void EditorScene::island_panel(Editor& editor, Ui& ui, World& world, PhysWorld& phys, Entity& sel)
{
    if (trigger_sync_ != editor.selection()) {
        trigger_sync_ = editor.selection();
        std::snprintf(trigger_name_, sizeof(trigger_name_), "%s", sel.mesh_name.c_str());
    }
    ui.text_field("island name", trigger_name_, sizeof(trigger_name_));
    const std::string_view typed{trigger_name_};
    if (sel.mesh_name != typed && !typed.empty() && typed.find('>') == std::string_view::npos
        && typed != "ground" && !island_named(world, typed)) {
        FixedString<32> old_name;
        old_name.assign(sel.mesh_name.view());
        for (u32 idx : world.entities().live_indices()) {
            Entity* l = world.entities().at(idx);
            if (!l || l->kind != EntityKind::IslandLink) {
                continue;
            }
            const std::string_view spec = l->mesh_name.view();
            const size_t split = spec.find('>');
            if (split == std::string_view::npos) {
                continue;
            }
            const std::string_view a = spec.substr(0, split);
            const std::string_view b = spec.substr(split + 1);
            if (a != old_name.view() && b != old_name.view()) {
                continue;
            }
            FixedString<64> renamed;
            const std::string_view na = a == old_name.view() ? typed : a;
            const std::string_view nb = b == old_name.view() ? typed : b;
            renamed.format("%.*s>%.*s", static_cast<int>(na.size()), na.data(), static_cast<int>(nb.size()),
                           nb.data());
            l->mesh_name.assign(renamed.view());
        }
        sel.mesh_name.assign(typed);
        editor.mark_dirty();
    }
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;
    f32 roll = 0.0f;
    quat_to_euler(sel.rot, yaw, pitch, roll);
    f32 pitch_deg = pitch * kRadToDeg;
    f32 roll_deg = roll * kRadToDeg;
    if (ui.slider("pitch", pitch_deg, -180.0f, 180.0f) || ui.slider("roll", roll_deg, -180.0f, 180.0f)) {
        sel.rot = quat_from_euler(yaw, pitch_deg * kDegToRad, roll_deg * kDegToRad);
        editor.mark_dirty();
    }
    if (ui.slider("radius", sel.half.x, 3.0f, kIslandMaxRadius) || ui.slider("depth", sel.aux_value, 2.0f, 30.0f)) {
        editor.mark_dirty();
    }
    f32 seed = static_cast<f32>(sel.aux_data);
    if (ui.slider("seed", seed, 0.0f, 999.0f)) {
        sel.aux_data = static_cast<u32>(seed + 0.5f);
        editor.mark_dirty();
    }
    f32 style = static_cast<f32>(sel.aux_kind & 0xFFu);
    bool crater = ((sel.aux_kind >> 8) & 1u) != 0;
    const bool style_changed = ui.slider("style", style, 0.0f, 1.0f);
    const bool crater_changed = ui.checkbox("crater below", crater);
    if (style_changed || crater_changed) {
        sel.aux_kind = (static_cast<u32>(style + 0.5f) & 0xFFu) | ((crater ? 1u : 0u) << 8);
        editor.mark_dirty();
    }
    if (ui.button("link to ground")) {
        add_link(editor, world, phys, sel.mesh_name.view(), "ground");
        return;
    }
    ui.text_field("link target", link_target_, sizeof(link_target_));
    if (ui.button("add link")) {
        FixedString<32> from;
        from.assign(sel.mesh_name.view());
        add_link(editor, world, phys, from.view(), std::string_view{link_target_});
    }
}

void EditorScene::link_panel(Editor& editor, Ui& ui, Entity& sel)
{
    ui.label("link: %s", sel.mesh_name.c_str());
    if (ui.slider("width", sel.aux_value, 3.0f, 16.0f)) {
        editor.mark_dirty();
    }
    const std::string_view spec = sel.mesh_name.view();
    if (spec.size() > 7 && spec.substr(spec.size() - 7) == ">ground") {
        bool pinned = (sel.aux_kind & 1u) != 0;
        if (ui.checkbox("pin ground point", pinned)) {
            sel.aux_kind = pinned ? 1u : 0u;
            editor.mark_dirty();
        }
        if (pinned) {
            ui.label("move the gizmo to place it");
        }
    }
}

bool EditorScene::update_car_boxes(Editor& editor, const Input& input, const Camera& cam,
                                   PhysWorld& phys, Vehicle& veh, InteractBoxes& boxes,
                                   Vec2 viewport, bool over_panel)
{
    RigidBody* body = phys.body(veh.body());
    if (!body) {
        return false;
    }
    const Vec3 com = veh.config().com_offset;
    Gizmo& gizmo = editor.gizmo();

    if (box_sel_ >= 0) {
        gizmo.set_mode(GizmoMode::Translate);
        if (!gizmo.dragging()) {
            gizmo.set_basis(body->rot);
        }
        InteractBox& box = boxes.box(static_cast<u32>(box_sel_));
        Vec3 world_center = body->pos + rotate(body->rot, box.center - com);
        const Vec3 prev = world_center;

        bool consumed = false;
        if (!over_panel || gizmo.dragging()) {
            GizmoTarget target;
            target.pos = &world_center;
            consumed = gizmo.update(input, cam, viewport, target, GizmoSnap{});
        }
        if (world_center != prev) {
            box.center = rotate(conjugate(body->rot), world_center - body->pos) + com;
        }
        if (consumed) {
            return true;
        }
    }

    if (over_panel || !input.pressed(MouseButton::Left) || input.down(MouseButton::Right)) {
        return false;
    }

    const Ray local = to_body_local(*body, cam.mouse_ray(input.mouse_pos(), viewport));
    f32 best_t = 1e30f;
    i32 best = -1;
    for (u32 i = 0; i < IBOX_COUNT; i++) {
        const InteractBox& box = boxes.box(i);
        const Vec3 center = box.center - com;
        const Aabb bounds{center - box.half, center + box.half};
        f32 t = 0.0f;
        if (ray_vs_aabb(local, bounds, best_t, &t) && t < best_t) {
            best_t = t;
            best = static_cast<i32>(i);
        }
    }
    if (best < 0) {
        return false;
    }
    box_sel_ = best;
    editor.clear_selection();
    return true;
}

bool EditorScene::update_placement(Editor& editor, const Input& input, const Camera& cam,
                                   World& world, PhysWorld& phys, Vec2 viewport,
                                   bool over_panel)
{
    if (input.pressed(Key::Escape)) {
        clear_placement();
        return true;
    }

    const Ray ray = cam.mouse_ray(input.mouse_pos(), viewport);
    PhysRayHit hit;
    if (phys.raycast(ray, kPlaceRange, &hit)) {
        preview_valid_ = true;
        preview_pos_ = hit.point;
    }
    if (!preview_valid_ || over_panel || !input.pressed(MouseButton::Left)
        || input.down(MouseButton::Right)) {
        return true;
    }

    if (place_mesh_ >= 0) {
        const EntityHandle handle = world.spawn(entity_kind_of(spawn_kind_), preview_pos_,
                                                quat_identity(), 1.0f,
                                                meshes_[place_mesh_].view(),
                                                kEntityFlagCollides);
        editor.select(world, handle);
        editor.push_create(world, phys, handle);
        editor.status("placed (collision after save+reload)");
    } else {
        const ItemKind kind = static_cast<ItemKind>(place_item_);
        Item item;
        item.kind = kind;
        item.condition = 1.0f;
        item.aux = 0;

        Vec3 pos = preview_pos_;
        pos.y += item_cargo_half(kind).y + 0.10f;

        const EntityHandle handle = interact_spawn_pickup(world, phys, item, pos, 0.0f, Vec3{});
        editor.select(world, handle);
        editor.push_create(world, phys, handle);
        editor.status("placed pickup");
    }
    return true;
}

bool EditorScene::update(Editor& editor, const Input& input, const Camera& cam, World& world,
                         PhysWorld& phys, const Terrain& terrain, Vehicle* veh,
                         InteractBoxes* boxes, Vec2 viewport, bool over_panel)
{
    (void)terrain;
    preview_valid_ = false;

    if (car_boxes_on_ && veh && boxes
        && update_car_boxes(editor, input, cam, phys, *veh, *boxes, viewport, over_panel)) {
        return true;
    }
    if (place_mesh_ >= 0 || place_item_ >= 0) {
        return update_placement(editor, input, cam, world, phys, viewport, over_panel);
    }
    return false;
}


void EditorScene::palette_panel(Editor& editor, Ui& ui, const Camera& cam, World& world,
                                PhysWorld& phys, const Terrain& terrain, f32 px)
{
    static const char* kKindNames[3] = {"kind: static", "kind: tree", "kind: building"};

    ui.panel_begin("palette", px, 16.0f, kScenePanelWidth);
    if (ui.list_item(palette_tab_ == 0 ? "[meshes]" : " meshes", palette_tab_ == 0)) {
        palette_tab_ = 0;
    }
    if (ui.list_item(palette_tab_ == 1 ? "[items]" : " items", palette_tab_ == 1)) {
        palette_tab_ = 1;
    }

    if (palette_tab_ == 0) {
        if (ui.list_item(kKindNames[static_cast<u32>(spawn_kind_)], false)) {
            cycle_spawn_kind();
        }
        if (ui.button("scroll")) {
            mesh_scroll_ += kSceneListPage;
            if (mesh_scroll_ >= static_cast<i32>(mesh_count_)) {
                mesh_scroll_ = 0;
            }
        }
        for (i32 i = mesh_scroll_;
             i < static_cast<i32>(mesh_count_) && i < mesh_scroll_ + kSceneListPage; i++) {
            if (ui.list_item(meshes_[i].view(), i == place_mesh_)) {
                set_place_mesh(i);
            }
        }
    } else {
        for (i32 k = 1; k < static_cast<i32>(ITEM_KIND_COUNT); k++) {
            if (ui.list_item(item_id(static_cast<ItemKind>(k)), k == place_item_)) {
                set_place_item(k);
            }
        }
    }

    if (ui.button("add trigger")) {
        add_trigger(editor, cam, world, phys, terrain);
    }
    if (ui.button("add gravity")) {
        add_gravity(editor, cam, world, phys);
    }
    if (ui.button("add ghost")) {
        add_ghost_spawn(editor, cam, world, phys, terrain);
    }
    if (ui.button("add prop")) {
        add_scene_thing(editor, cam, world, phys, terrain, EntityKind::Prop);
    }
    if (ui.button("add spawn point")) {
        add_scene_thing(editor, cam, world, phys, terrain, EntityKind::SpawnPoint);
    }
    if (ui.button("add bench")) {
        add_scene_thing(editor, cam, world, phys, terrain, EntityKind::Bench);
    }
    if (ui.button("add island")) {
        add_island(editor, cam, world, phys, terrain);
    }
    if (place_mesh_ >= 0 || place_item_ >= 0) {
        ui.label("click ground to place, esc stops");
    }
    ui.panel_end();
}

void EditorScene::outliner_panel(Editor& editor, Ui& ui, World& world, f32 px)
{
    ui.panel_begin("entities", px, 512.0f, kScenePanelWidth);
    if (ui.button("scroll")) {
        outliner_scroll_ += kSceneListPage;
        if (outliner_scroll_ >= static_cast<i32>(world.count())) {
            outliner_scroll_ = 0;
        }
    }

    const Pool<Entity>& pool = world.entities();
    i32 row = 0;
    i32 shown = 0;
    for (u32 idx = 0; idx < pool.capacity() && shown < kSceneListPage; idx++) {
        const Entity* e = pool.at(idx);
        if (!e) {
            continue;
        }
        if (row++ < outliner_scroll_) {
            continue;
        }

        FixedString<64> label;
        if (e->kind == EntityKind::PartPickup) {
            const std::string_view id = item_id(static_cast<ItemKind>(e->aux_kind));
            label.format("%u item %.*s", idx, static_cast<int>(id.size()), id.data());
        } else if (e->kind == EntityKind::Trigger) {
            label.format("%u trig %s", idx, e->mesh_name.c_str());
        } else if (e->kind == EntityKind::Gravity) {
            label.format("%u grav %s", idx, e->mesh_name.c_str());
        } else if (e->kind == EntityKind::Island) {
            label.format("%u isle %s", idx, e->mesh_name.c_str());
        } else if (e->kind == EntityKind::GhostSpawn) {
            label.format("%u ghost %s x%u", idx, e->mesh_name.c_str(), e->aux_kind);
        } else if (e->kind == EntityKind::Prop) {
            const std::string_view surface = surface_name(static_cast<u8>(e->aux_kind));
            label.format("%u prop %.*s", idx, static_cast<int>(surface.size()), surface.data());
        } else if (e->kind == EntityKind::SpawnPoint) {
            label.format("%u spawn %u", idx, e->aux_kind + 1);
        } else if (e->kind == EntityKind::Bench) {
            label.format("%u bench", idx);
        } else if (e->kind == EntityKind::IslandLink) {
            label.format("%u link %s", idx, e->mesh_name.c_str());
        } else {
            label.format("%u %s", idx, e->mesh_name.c_str());
        }

        const EntityHandle handle = pool.handle_at(idx);
        if (ui.list_item(label.view(), handle == editor.selection())) {
            editor.select(world, handle);
            clear_placement();
        }
        shown++;
    }
    ui.panel_end();
}

void EditorScene::detail_panel(Editor& editor, Ui& ui, World& world, PhysWorld& phys,
                               InteractBoxes* boxes, const TapeLibrary* tapes)
{
    Entity* sel = world.entity(editor.selection());
    const bool relevant = car_boxes_on_
                       || (sel && (sel->kind == EntityKind::PartPickup
                                   || sel->kind == EntityKind::Trigger
                                   || sel->kind == EntityKind::Gravity
                                   || sel->kind == EntityKind::GhostSpawn
                                   || sel->kind == EntityKind::Prop
                                   || sel->kind == EntityKind::SpawnPoint
                                   || sel->kind == EntityKind::Island
                                   || sel->kind == EntityKind::IslandLink));

    ui.panel_begin("tuning", 292.0f, 16.0f, 250.0f);
    if (ui.checkbox("car interact boxes", car_boxes_on_) && !car_boxes_on_) {
        box_sel_ = -1;
    }

    if (car_boxes_on_ && boxes) {
        if (box_sel_ >= 0) {
            InteractBox& box = boxes->box(static_cast<u32>(box_sel_));
            ui.label("box: %s", box.name.c_str());
            ui.label("center %.2f %.2f %.2f", static_cast<f64>(box.center.x),
                     static_cast<f64>(box.center.y), static_cast<f64>(box.center.z));
            ui.slider("half x", box.half.x, 0.01f, 1.6f);
            ui.slider("half y", box.half.y, 0.01f, 1.6f);
            ui.slider("half z", box.half.z, 0.01f, 1.6f);
        } else {
            ui.label("click a box to tune it");
        }
        if (ui.button("save boxes")) {
            editor.status(boxes->save() ? "saved interact boxes" : "box save failed");
        }
    }

    if (sel && sel->kind == EntityKind::PartPickup) {
        const ItemKind kind = static_cast<ItemKind>(sel->aux_kind);
        const std::string_view name = item_name(kind);
        ui.label("pickup: %.*s", static_cast<int>(name.size()), name.data());
        if (ui.slider("condition", sel->aux_value, 0.0f, 1.0f)) {
            editor.mark_dirty();
        }

        if (kind == ITEM_CASSETTE && tapes) {
            const std::string_view current = tapes->label(static_cast<i32>(sel->aux_data));
            ui.label("tape: %.*s", static_cast<int>(current.size()), current.data());

            const i32 option_count = static_cast<i32>(tapes->count()) + 1;
            if (option_count > kSceneListPage && ui.button("scroll tapes")) {
                tape_scroll_ += kSceneListPage;
                if (tape_scroll_ >= option_count) {
                    tape_scroll_ = 0;
                }
            }
            for (i32 a = tape_scroll_;
                 a < option_count && a < tape_scroll_ + kSceneListPage; a++) {
                if (ui.list_item(tapes->label(a), static_cast<u32>(a) == sel->aux_data)) {
                    sel->aux_data = static_cast<u32>(a);
                    editor.mark_dirty();
                }
            }
        } else if (kind == ITEM_FLOPPY) {
            const std::string_view current = disk_label(static_cast<i32>(sel->aux_data));
            ui.label("disk: %.*s", static_cast<int>(current.size()), current.data());
            for (i32 d = 0; d < DISK_COUNT; d++) {
                if (ui.list_item(disk_label(d), static_cast<u32>(d) == sel->aux_data)) {
                    sel->aux_data = static_cast<u32>(d);
                    editor.mark_dirty();
                }
            }
        } else {
            f32 aux = static_cast<f32>(sel->aux_data);
            if (ui.slider("aux", aux, 0.0f, 62.0f)) {
                sel->aux_data = static_cast<u32>(aux + 0.5f);
                editor.mark_dirty();
            }
        }
    }

    if (sel && sel->kind == EntityKind::Trigger) {
        if (trigger_sync_ != editor.selection()) {
            trigger_sync_ = editor.selection();
            std::snprintf(trigger_name_, sizeof(trigger_name_), "%s", sel->mesh_name.c_str());
        }
        ui.text_field("trigger name", trigger_name_, sizeof(trigger_name_));
        if (sel->mesh_name != trigger_name_) {
            sel->mesh_name.assign(trigger_name_);
            editor.mark_dirty();
        }
        if (ui.slider("half x", sel->half.x, 0.2f, 24.0f)
            || ui.slider("half y", sel->half.y, 0.2f, 24.0f)
            || ui.slider("half z", sel->half.z, 0.2f, 24.0f)) {
            editor.mark_dirty();
        }
        f32 action = static_cast<f32>(sel->aux_kind);
        if (ui.slider("action id", action, 0.0f, 8.0f)) {
            sel->aux_kind = static_cast<u32>(action + 0.5f);
            editor.mark_dirty();
        }
        if (ui.slider("param", sel->aux_value, 0.0f, 10.0f)) {
            editor.mark_dirty();
        }
    }

    if (sel && sel->kind == EntityKind::Gravity) {
        if (trigger_sync_ != editor.selection()) {
            trigger_sync_ = editor.selection();
            std::snprintf(trigger_name_, sizeof(trigger_name_), "%s", sel->mesh_name.c_str());
        }
        ui.text_field("volume name", trigger_name_, sizeof(trigger_name_));
        if (sel->mesh_name != trigger_name_) {
            sel->mesh_name.assign(trigger_name_);
            editor.mark_dirty();
        }
        if (ui.slider("half x", sel->half.x, 0.5f, 40.0f)
            || ui.slider("half y", sel->half.y, 0.5f, 40.0f)
            || ui.slider("half z", sel->half.z, 0.5f, 40.0f)
            || ui.slider("falloff", sel->scale, 0.0f, 30.0f)
            || ui.slider("strength", sel->aux_value, 0.0f, 30.0f)) {
            editor.mark_dirty();
        }
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;
        f32 roll = 0.0f;
        quat_to_euler(sel->rot, yaw, pitch, roll);
        f32 pitch_deg = pitch * kRadToDeg;
        f32 roll_deg = roll * kRadToDeg;
        if (ui.slider("pitch", pitch_deg, -180.0f, 180.0f)
            || ui.slider("roll", roll_deg, -180.0f, 180.0f)) {
            sel->rot = quat_from_euler(yaw, pitch_deg * kDegToRad, roll_deg * kDegToRad);
            editor.mark_dirty();
        }
        f32 shape = static_cast<f32>(sel->aux_kind & 0xFu);
        f32 mode = static_cast<f32>(sel->aux_kind >> 4);
        if (ui.slider("sphere", shape, 0.0f, 1.0f) || ui.slider("mode dir/curl/point", mode, 0.0f, 2.0f)) {
            sel->aux_kind = (static_cast<u32>(shape + 0.5f) & 0xFu) | (static_cast<u32>(mode + 0.5f) << 4);
            editor.mark_dirty();
        }
        f32 sector = static_cast<f32>(sel->aux_data ? sel->aux_data : 90u);
        if (ui.slider("curl sector", sector, 1.0f, 180.0f)) {
            sel->aux_data = static_cast<u32>(sector + 0.5f);
            editor.mark_dirty();
        }
    }

    if (sel && sel->kind == EntityKind::Prop) {
        if (ui.slider("half x", sel->half.x, 0.02f, 30.0f) || ui.slider("half y", sel->half.y, 0.02f, 10.0f)
            || ui.slider("half z", sel->half.z, 0.02f, 30.0f)) {
            editor.mark_dirty();
        }
        for (const char* surface : {"concrete", "wood", "steel", "ground"}) {
            if (ui.list_item(surface, surface_name(static_cast<u8>(sel->aux_kind)) == surface)) {
                sel->aux_kind = surface_from_name(surface);
                editor.mark_dirty();
            }
        }
        f32 rgb[3] = {static_cast<f32>((sel->aux_data >> 16) & 0xFFu) / 255.0f, static_cast<f32>((sel->aux_data >> 8) & 0xFFu) / 255.0f,
                      static_cast<f32>(sel->aux_data & 0xFFu) / 255.0f};
        if (ui.slider("red", rgb[0], 0.0f, 1.0f) || ui.slider("green", rgb[1], 0.0f, 1.0f) || ui.slider("blue", rgb[2], 0.0f, 1.0f)) {
            const auto byte = [](f32 v) { return static_cast<u32>(f_clamp01(v) * 255.0f + 0.5f); };
            sel->aux_data = (byte(rgb[0]) << 16) | (byte(rgb[1]) << 8) | byte(rgb[2]);
            editor.mark_dirty();
        }
    }

    if (sel && sel->kind == EntityKind::SpawnPoint) {
        f32 order = static_cast<f32>(sel->aux_kind);
        if (ui.slider("player", order, 0.0f, 7.0f)) {
            sel->aux_kind = static_cast<u32>(order + 0.5f);
            editor.mark_dirty();
        }
    }

    if (sel && sel->kind == EntityKind::GhostSpawn) {
        ui.label("ghost: %s", sel->mesh_name.c_str());
        for (const std::string& type : ghost_types_) {
            if (ui.list_item(type, sel->mesh_name.view() == type)) {
                sel->mesh_name.assign(type);
                editor.mark_dirty();
            }
        }
        f32 count = static_cast<f32>(sel->aux_kind);
        if (ui.slider("count", count, 1.0f, static_cast<f32>(kZoneMaxGhostsPerSpawn))) {
            sel->aux_kind = static_cast<u32>(count + 0.5f);
            editor.mark_dirty();
        }
    }

    if (sel && sel->kind == EntityKind::Island) {
        island_panel(editor, ui, world, phys, *sel);
    } else if (sel && sel->kind == EntityKind::IslandLink) {
        link_panel(editor, ui, *sel);
    }

    if (!relevant && !sel) {
        ui.label("nothing selected");
    }
    ui.panel_end();
}

void EditorScene::render(Editor& editor, Ui& ui, DebugDraw& debug, const Camera& cam,
                         World& world, PhysWorld& phys, const Terrain& terrain, Vehicle* veh,
                         InteractBoxes* boxes, const TapeLibrary* tapes, Vec2 viewport)
{
    if (preview_valid_) {
        debug.overlay(true);
        debug.sphere(preview_pos_, 0.35f, kDdGreen);
        debug.cross(preview_pos_, 1.2f, kDdGreen);
        debug.overlay(false);
    }

    if (car_boxes_on_ && veh && boxes) {
        if (const RigidBody* body = phys.body(veh->body())) {
            const Vec3 com = veh->config().com_offset;
            debug.overlay(true);
            for (u32 i = 0; i < IBOX_COUNT; i++) {
                const InteractBox& box = boxes->box(i);
                const Vec3 center = body->pos + rotate(body->rot, box.center - com);
                const bool is_sel = static_cast<i32>(i) == box_sel_;
                debug.obb(center, body->rot, box.half, is_sel ? kDdYellow : kDdOrange);
                if (is_sel) {
                    debug.text_3d(center + Vec3{0.0f, box.half.y + 0.12f, 0.0f}, 13.0f,
                                  kDdYellow, "%s", box.name.c_str());
                }
            }
            debug.overlay(false);

            if (box_sel_ >= 0) {
                const InteractBox& box = boxes->box(static_cast<u32>(box_sel_));
                editor.gizmo().render(debug, cam,
                                      body->pos + rotate(body->rot, box.center - com));
            }
        }
    }

    const f32 px = viewport.x - kScenePanelWidth - 16.0f;
    palette_panel(editor, ui, cam, world, phys, terrain, px);
    outliner_panel(editor, ui, world, px);
    detail_panel(editor, ui, world, phys, boxes, tapes);
}

} // namespace anom
