#include "world/zone.h"
#include "world/pickup_body.h"
#include "assets/mesh_data.h"
#include "carsys/items.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "physics/gravity_field.h"
#include "world/islands/island_field.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "world/tree_gen.h"
#include "world/entity.h"
#include "world/terrain.h"

#include <charconv>
#include <cstdio>

namespace anom {
namespace {
class Tokens {
public:
    explicit Tokens(std::string_view line) : cursor_(line) {}

    std::string_view next()
    {
        const std::size_t begin = cursor_.find_first_not_of(" \t");
        if (begin == std::string_view::npos) {
            cursor_ = {};
            return {};
        }
        const std::size_t end = cursor_.find_first_of(" \t", begin);
        const std::string_view token = cursor_.substr(
            begin, end == std::string_view::npos ? std::string_view::npos : end - begin);
        cursor_ = end == std::string_view::npos ? std::string_view{} : cursor_.substr(end);
        count_++;
        return token;
    }

    f32 next_f32(f32 fallback = 0.0f)
    {
        const std::string_view token = next();
        if (token.empty()) {
            return fallback;
        }
        f32 value = fallback;
        std::from_chars(token.data(), token.data() + token.size(), value);
        return value;
    }

    i32 next_i32(i32 fallback = 0)
    {
        const std::string_view token = next();
        if (token.empty()) {
            return fallback;
        }
        i32 value = fallback;
        std::from_chars(token.data(), token.data() + token.size(), value);
        return value;
    }

    u32 consumed() const { return count_; }

private:
    std::string_view cursor_;
    u32 count_ = 0;
};

EntityKind kind_from_str(std::string_view s)
{
    if (s == "tree") {
        return EntityKind::Tree;
    }
    if (s == "building") {
        return EntityKind::Building;
    }
    return EntityKind::StaticMesh;
}

void add_mesh_collision(PhysWorld& phys, Arena& scratch, std::string_view mesh_name,
                        EntityKind kind, Vec3 pos, Quat rot, f32 scale)
{
    ArenaScope scope(scratch);

    FixedString<256> path;
    MeshData data;
    bool loaded = false;

    path.format("assets/meshes/%.*s_col.amsh", static_cast<int>(mesh_name.size()),
                mesh_name.data());
    if (fs::exists(path.view())) {
        loaded = load_mesh(path.view(), scratch, data) == MeshParseError::Ok;
        if (loaded) {
            kind = EntityKind::Building;
        }
    }
    if (!loaded) {
        path.format("assets/meshes/%.*s.amsh", static_cast<int>(mesh_name.size()),
                    mesh_name.data());
        loaded = load_mesh(path.view(), scratch, data) == MeshParseError::Ok;
    }
    if (!loaded) {
        return;
    }

    const Mat3 rotation = quat_to_mat3(rot);
    for (const AmshSubmesh& sub : data.submeshes) {
        if (kind == EntityKind::Tree && std::string_view(sub.material) != "bark") {
            continue;
        }
        for (u32 i = 0; i + 2 < sub.index_count; i += 3) {
            Vec3 tri[3];
            for (u32 k = 0; k < 3; k++) {
                const AmshVertex& v = data.vertices[data.indices[sub.first_index + i + k]];
                const Vec3 local = Vec3{v.pos[0], v.pos[1], v.pos[2]} * scale;
                tri[k] = pos + rotation * local;
            }
            phys.add_static_tri(tri[0], tri[1], tri[2]);
        }
    }
}

u32 tree_variant_for(Vec3 pos)
{
    const i32 xi = static_cast<i32>(std::floor(pos.x * 4.0f));
    const i32 zi = static_cast<i32>(std::floor(pos.z * 4.0f));
    u32 h = static_cast<u32>(xi * 73856093) ^ static_cast<u32>(zi * 19349663);
    h ^= h >> 13;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return h % kTreeVariants;
}

void add_tree_collision(PhysWorld& phys, Arena& scratch, u32 variant, Vec3 pos, Quat rot,
                        f32 scale)
{
    ArenaScope scope(scratch);
    TreeData data;
    if (!tree_generate(variant, scratch, data)) {
        return;
    }
    const Mat3 rotation = quat_to_mat3(rot);
    for (u32 i = 0; i + 2 < data.trunk_index_count; i += 3) {
        Vec3 tri[3];
        for (u32 k = 0; k < 3; k++) {
            const Vec3 local = data.vertices[data.indices[i + k]].pos * scale;
            tri[k] = pos + rotation * local;
        }
        phys.add_static_tri(tri[0], tri[1], tri[2]);
    }
}

void spawn_entity(World& world, PhysWorld& phys, Arena& scratch, const Terrain& terrain,
                  std::string_view line)
{
    Tokens t(line);
    const std::string_view kind_str = t.next();
    const std::string_view mesh_name = t.next();
    const f32 x = t.next_f32();
    const f32 z = t.next_f32();
    const f32 yaw_deg = t.next_f32();
    const f32 scale = t.next_f32(1.0f);
    if (t.consumed() < 6 || mesh_name.empty()) {
        log_warn("zone: malformed spawn line: %.*s", static_cast<int>(line.size()), line.data());
        return;
    }
    const f32 yoff = t.next_f32(0.0f);
    const f32 pitch_deg = t.next_f32(0.0f);
    const f32 roll_deg = t.next_f32(0.0f);

    const EntityKind kind = kind_from_str(kind_str);
    const Vec3 pos{x, terrain.heightfield().sample(x, z) + yoff, z};
    const Quat rot = quat_from_euler(yaw_deg * kDegToRad, pitch_deg * kDegToRad,
                                     roll_deg * kDegToRad);

    const EntityHandle handle = world.spawn(kind, pos, rot, scale, mesh_name,
                                            kEntityFlagCollides);
    if (kind == EntityKind::Tree) {
        const u32 variant = tree_variant_for(pos);
        if (Entity* e = world.entity(handle)) {
            e->aux_kind = variant;
        }
        add_tree_collision(phys, scratch, variant, pos, rot, scale);
        return;
    }
    add_mesh_collision(phys, scratch, mesh_name, kind, pos, rot, scale);
}

void spawn_trigger(World& world, const Terrain& terrain, std::string_view line)
{
    Tokens t(line);
    const std::string_view name = t.next();
    const f32 x = t.next_f32();
    const f32 yoff = t.next_f32();
    const f32 z = t.next_f32();
    const f32 hx = t.next_f32();
    const f32 hy = t.next_f32();
    const f32 hz = t.next_f32();
    const f32 yaw_deg = t.next_f32();
    const i32 action = t.next_i32();
    if (t.consumed() < 9 || name.empty()) {
        log_warn("zone: malformed trigger line: %.*s", static_cast<int>(line.size()),
                 line.data());
        return;
    }
    const f32 param = t.next_f32(0.0f);

    const Vec3 pos{x, terrain.heightfield().sample(x, z) + yoff, z};
    const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, yaw_deg * kDegToRad);
    const EntityHandle handle = world.spawn(EntityKind::Trigger, pos, rot, 1.0f, "",
                                            kEntityFlagInteractable);
    if (Entity* e = world.entity(handle)) {
        e->mesh_name.assign(name);
        e->half = Vec3{hx, hy, hz};
        e->aux_kind = static_cast<u32>(action);
        e->aux_value = param;
    }
}

void spawn_gravity(World& world, const Terrain& terrain, std::string_view line)
{
    Tokens t(line);
    const std::string_view name = t.next();
    const f32 x = t.next_f32();
    const f32 yoff = t.next_f32();
    const f32 z = t.next_f32();
    const f32 hx = t.next_f32();
    const f32 hy = t.next_f32();
    const f32 hz = t.next_f32();
    const f32 yaw_deg = t.next_f32();
    const f32 pitch_deg = t.next_f32();
    const f32 roll_deg = t.next_f32();
    if (t.consumed() < 10 || name.empty()) {
        log_warn("zone: malformed gravity line: %.*s", static_cast<int>(line.size()),
                 line.data());
        return;
    }
    const f32 falloff = t.next_f32(4.0f);
    const f32 strength = t.next_f32(kDefaultGravity);
    const i32 shape = t.next_i32(0);
    const i32 mode = t.next_i32(0);
    const f32 sector_deg = t.next_f32(90.0f);

    const Vec3 pos{x, terrain.heightfield().sample(x, z) + yoff, z};
    const Quat rot = quat_from_euler(yaw_deg * kDegToRad, pitch_deg * kDegToRad,
                                     roll_deg * kDegToRad);
    const EntityHandle handle = world.spawn(EntityKind::Gravity, pos, rot, falloff, "", 0);
    if (Entity* e = world.entity(handle)) {
        e->mesh_name.assign(name);
        e->half = Vec3{hx, hy, hz};
        e->aux_kind = (static_cast<u32>(shape) & 0xFu) | (static_cast<u32>(mode) << 4);
        e->aux_value = strength;
        e->aux_data = static_cast<u32>(f_clamp(sector_deg, 1.0f, 180.0f) + 0.5f);
    }
}

void spawn_island(World& world, std::string_view line)
{
    Tokens t(line);
    const std::string_view name = t.next();
    const f32 x = t.next_f32();
    const f32 y = t.next_f32();
    const f32 z = t.next_f32();
    const f32 yaw_deg = t.next_f32();
    const f32 pitch_deg = t.next_f32();
    const f32 roll_deg = t.next_f32();
    const f32 radius = t.next_f32();
    if (t.consumed() < 8 || name.empty()) {
        log_warn("zone: malformed island line: %.*s", static_cast<int>(line.size()), line.data());
        return;
    }
    const f32 depth = t.next_f32(0.0f);
    const i32 seed = t.next_i32(1);
    const i32 style = t.next_i32(0);
    const i32 crater = t.next_i32(1);
    const Quat rot = quat_from_euler(yaw_deg * kDegToRad, pitch_deg * kDegToRad, roll_deg * kDegToRad);
    const EntityHandle handle = world.spawn(EntityKind::Island, Vec3{x, y, z}, rot, 1.0f, name, 0);
    if (Entity* e = world.entity(handle)) {
        e->half = Vec3{radius, 0.0f, 0.0f};
        e->aux_value = depth;
        e->aux_data = static_cast<u32>(seed);
        e->aux_kind = (static_cast<u32>(style) & 0xFFu) | ((crater ? 1u : 0u) << 8);
    }
}

void spawn_link(World& world, const Terrain& terrain, std::string_view line)
{
    Tokens t(line);
    const std::string_view from = t.next();
    const std::string_view to = t.next();
    if (t.consumed() < 2 || from.empty() || to.empty()) {
        log_warn("zone: malformed link line: %.*s", static_cast<int>(line.size()), line.data());
        return;
    }
    const f32 width = t.next_f32(kLinkDefaultWidth);
    const f32 gx = t.next_f32(0.0f);
    const f32 gz = t.next_f32(0.0f);
    const bool has_ground = t.consumed() >= 5;
    FixedString<32> spec;
    spec.format("%.*s>%.*s", static_cast<int>(from.size()), from.data(), static_cast<int>(to.size()), to.data());
    const Vec3 pos{gx, has_ground ? terrain.heightfield().sample(gx, gz) : 0.0f, gz};
    const EntityHandle handle = world.spawn(EntityKind::IslandLink, pos, quat_identity(), 1.0f, spec.view(), 0);
    if (Entity* e = world.entity(handle)) {
        e->aux_value = width;
        e->aux_kind = has_ground ? 1u : 0u;
    }
}

void spawn_tower(World& world, PhysWorld& phys, Arena& scratch, const Terrain& terrain,
                 std::string_view line, ZoneSpawn* out_spawn)
{
    Tokens t(line);
    const f32 x = t.next_f32();
    const f32 z = t.next_f32();
    const f32 yaw_deg = t.next_f32();
    if (t.consumed() < 3) {
        log_warn("zone: malformed tower line: %.*s", static_cast<int>(line.size()), line.data());
        return;
    }

    const Vec3 pos{x, terrain.heightfield().sample(x, z), z};
    const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, yaw_deg * kDegToRad);
    world.spawn(EntityKind::Building, pos, rot, 1.0f, "relay_tower",
                kEntityFlagCollides | kEntityFlagTower);
    add_mesh_collision(phys, scratch, "relay_tower", EntityKind::Building, pos, rot, 1.0f);

    if (out_spawn) {
        out_spawn->tower_present = true;
        out_spawn->tower_x = x;
        out_spawn->tower_z = z;
        out_spawn->tower_yaw_deg = yaw_deg;
    }
}

void collect_pickup(std::string_view line, ZonePickups* out_pickups)
{
    if (!out_pickups || out_pickups->count >= kZoneMaxPickups) {
        return;
    }
    Tokens t(line);
    ZonePickup p;
    const std::string_view item = t.next();
    p.x = t.next_f32();
    p.z = t.next_f32();
    p.yaw_deg = t.next_f32();
    if (t.consumed() < 4 || item.empty()) {
        log_warn("zone: malformed pickup line: %.*s", static_cast<int>(line.size()), line.data());
        return;
    }
    p.item.assign(item);
    p.condition = t.next_f32(1.0f);
    p.aux = t.next_i32(0);
    out_pickups->items[out_pickups->count++] = p;
}

u32 spawn_from_config(World& world, PhysWorld& phys, Arena& scratch, const Terrain& terrain,
                      const Config& cfg, ZoneSpawn* out_spawn, ZonePickups* out_pickups)
{
    if (out_spawn) {
        out_spawn->tower_present = false;
    }
    if (out_pickups) {
        out_pickups->count = 0;
    }

    u32 count = 0;
    for (const Config::Entry& entry : cfg.entries()) {
        if (entry.key == "entities.spawn") {
            spawn_entity(world, phys, scratch, terrain, entry.value);
            count++;
        } else if (entry.key == "entities.trigger") {
            spawn_trigger(world, terrain, entry.value);
            count++;
        } else if (entry.key == "entities.island") {
            spawn_island(world, entry.value);
            count++;
        } else if (entry.key == "entities.link") {
            spawn_link(world, terrain, entry.value);
            count++;
        } else if (entry.key == "entities.gravity") {
            spawn_gravity(world, terrain, entry.value);
            count++;
        } else if (entry.key == "entities.tower") {
            spawn_tower(world, phys, scratch, terrain, entry.value, out_spawn);
            count++;
        } else if (entry.key == "entities.pickup") {
            collect_pickup(entry.value, out_pickups);
        }
    }
    return count;
}

std::string_view kind_to_str(EntityKind kind)
{
    switch (kind) {
    case EntityKind::Tree:
        return "tree";
    case EntityKind::Building:
        return "building";
    default:
        return "static";
    }
}

std::string_view line_key(std::string_view line)
{
    const std::size_t begin = line.find_first_not_of(" \t");
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t eq = line.find('=', begin);
    if (eq == std::string_view::npos) {
        return {};
    }
    const std::size_t end = line.find_last_not_of(" \t", eq - 1);
    if (end == std::string_view::npos || end < begin) {
        return {};
    }
    return line.substr(begin, end - begin + 1);
}

bool is_entity_key(std::string_view key)
{
    return key == "spawn" || key == "pickup" || key == "trigger" || key == "tower";
}

std::string_view trim_left(std::string_view line)
{
    const std::size_t begin = line.find_first_not_of(" \t");
    return begin == std::string_view::npos ? std::string_view{} : line.substr(begin);
}

void write_entities(std::FILE* out, const World& world, const PhysWorld& phys,
                    const Terrain& terrain, u32& out_written)
{
    const Heightfield& hf = terrain.heightfield();
    const Pool<Entity>& pool = world.entities();

    for (u32 idx = 0; idx < pool.capacity(); idx++) {
        const Entity* e = pool.at(idx);
        if (!e) {
            continue;
        }

        if (e->flags & kEntityFlagTower) {
            std::fprintf(out, "tower = %.3f %.3f %.2f\n", static_cast<f64>(e->pos.x),
                         static_cast<f64>(e->pos.z),
                         static_cast<f64>(quat_yaw(e->rot) * kRadToDeg));
            out_written++;
            continue;
        }

        if (e->kind == EntityKind::Trigger) {
            const f32 ground = hf.sample(e->pos.x, e->pos.z);
            std::fprintf(out, "trigger = %s %.3f %.3f %.3f %.3f %.3f %.3f %.2f %d %.3f\n",
                         e->mesh_name.empty() ? "unnamed" : e->mesh_name.c_str(),
                         static_cast<f64>(e->pos.x), static_cast<f64>(e->pos.y - ground),
                         static_cast<f64>(e->pos.z), static_cast<f64>(e->half.x),
                         static_cast<f64>(e->half.y), static_cast<f64>(e->half.z),
                         static_cast<f64>(quat_yaw(e->rot) * kRadToDeg),
                         static_cast<int>(e->aux_kind), static_cast<f64>(e->aux_value));
            out_written++;
            continue;
        }

        if (e->kind == EntityKind::Gravity) {
            f32 gyaw = 0.0f;
            f32 gpitch = 0.0f;
            f32 groll = 0.0f;
            quat_to_euler(e->rot, gyaw, gpitch, groll);
            const f32 ground = hf.sample(e->pos.x, e->pos.z);
            std::fprintf(out,
                         "gravity = %s %.3f %.3f %.3f %.3f %.3f %.3f %.2f %.2f %.2f %.3f %.3f %d %d %u\n",
                         e->mesh_name.empty() ? "unnamed" : e->mesh_name.c_str(),
                         static_cast<f64>(e->pos.x), static_cast<f64>(e->pos.y - ground),
                         static_cast<f64>(e->pos.z), static_cast<f64>(e->half.x),
                         static_cast<f64>(e->half.y), static_cast<f64>(e->half.z),
                         static_cast<f64>(gyaw * kRadToDeg), static_cast<f64>(gpitch * kRadToDeg),
                         static_cast<f64>(groll * kRadToDeg), static_cast<f64>(e->scale),
                         static_cast<f64>(e->aux_value), static_cast<int>(e->aux_kind & 0xFu),
                         static_cast<int>(e->aux_kind >> 4), e->aux_data ? e->aux_data : 90u);
            out_written++;
            continue;
        }

        if (e->kind == EntityKind::Island) {
            f32 iyaw = 0.0f;
            f32 ipitch = 0.0f;
            f32 iroll = 0.0f;
            quat_to_euler(e->rot, iyaw, ipitch, iroll);
            std::fprintf(out, "island = %s %.3f %.3f %.3f %.2f %.2f %.2f %.2f %.2f %u %u %u\n",
                         e->mesh_name.empty() ? "unnamed" : e->mesh_name.c_str(),
                         static_cast<f64>(e->pos.x), static_cast<f64>(e->pos.y),
                         static_cast<f64>(e->pos.z), static_cast<f64>(iyaw * kRadToDeg),
                         static_cast<f64>(ipitch * kRadToDeg), static_cast<f64>(iroll * kRadToDeg),
                         static_cast<f64>(e->half.x), static_cast<f64>(e->aux_value), e->aux_data,
                         e->aux_kind & 0xFFu, (e->aux_kind >> 8) & 1u);
            out_written++;
            continue;
        }

        if (e->kind == EntityKind::IslandLink) {
            const std::string_view spec = e->mesh_name.view();
            const size_t split = spec.find('>');
            if (split == std::string_view::npos) {
                continue;
            }
            const std::string_view from = spec.substr(0, split);
            const std::string_view to = spec.substr(split + 1);
            std::fprintf(out, "link = %.*s %.*s %.2f", static_cast<int>(from.size()), from.data(),
                         static_cast<int>(to.size()), to.data(), static_cast<f64>(e->aux_value));
            if (e->aux_kind & 1u) {
                std::fprintf(out, " %.3f %.3f", static_cast<f64>(e->pos.x), static_cast<f64>(e->pos.z));
            }
            std::fputc('\n', out);
            out_written++;
            continue;
        }

        if (e->kind == EntityKind::PartPickup) {
            const ItemKind item = static_cast<ItemKind>(e->aux_kind);
            Vec3 pos = e->pos + rotate(e->rot, item_mesh_center(item));
            Quat rot = e->rot * conjugate(item_cargo_rot(item));
            PickupState body;
            if (pickup_body_state(phys, *e, body)) {
                pos = body.pos;
                rot = body.rot;
            }
            const std::string_view id = item_id(item);
            std::fprintf(out, "pickup = %.*s %.3f %.3f %.2f %.3f %d\n",
                         static_cast<int>(id.size()), id.data(), static_cast<f64>(pos.x),
                         static_cast<f64>(pos.z),
                         static_cast<f64>(quat_yaw(rot) * kRadToDeg),
                         static_cast<f64>(e->aux_value), static_cast<int>(e->aux_data));
            out_written++;
            continue;
        }

        if (e->kind == EntityKind::Vehicle || e->mesh_name.empty()) {
            continue;
        }

        f32 yaw = 0.0f;
        f32 pitch = 0.0f;
        f32 roll = 0.0f;
        quat_to_euler(e->rot, yaw, pitch, roll);
        yaw *= kRadToDeg;
        pitch *= kRadToDeg;
        roll *= kRadToDeg;

        const f32 yoff = e->pos.y - hf.sample(e->pos.x, e->pos.z);
        std::fprintf(out, "spawn = %.*s %s %.3f %.3f %.2f %.3f",
                     static_cast<int>(kind_to_str(e->kind).size()), kind_to_str(e->kind).data(),
                     e->mesh_name.c_str(), static_cast<f64>(e->pos.x),
                     static_cast<f64>(e->pos.z), static_cast<f64>(yaw),
                     static_cast<f64>(e->scale));
        if (f_abs(yoff) > 0.01f || f_abs(pitch) > 0.01f || f_abs(roll) > 0.01f) {
            std::fprintf(out, " %.3f", static_cast<f64>(yoff));
        }
        if (f_abs(pitch) > 0.01f || f_abs(roll) > 0.01f) {
            std::fprintf(out, " %.2f %.2f", static_cast<f64>(pitch), static_cast<f64>(roll));
        }
        std::fputc('\n', out);
        out_written++;
    }
}

}

bool zone_load(std::string_view zone_dir, Arena& arena, Arena& scratch, World& world,
               PhysWorld& phys, Terrain& terrain, ZoneSpawn& out_spawn,
               ZonePickups* out_pickups)
{
    ArenaScope scope(scratch);

    FixedString<256> path;
    path.format("%.*s/zone.cfg", static_cast<int>(zone_dir.size()), zone_dir.data());
    const fs::FileData file = fs::read_entire_file(scratch, path.view());
    if (!file.valid()) {
        return false;
    }

    Config cfg;
    if (!cfg.parse(scratch, file.text())) {
        return false;
    }
    if (!terrain.load(arena, scratch, zone_dir, cfg)) {
        return false;
    }

    phys.statics_reserve(arena, kZoneMaxStaticTris);

    const u32 entity_count = spawn_from_config(world, phys, scratch, terrain, cfg, &out_spawn,
                                               out_pickups);
    phys.statics_build(arena);

    const Vec3 car_xz = cfg.get_vec3("spawn.car_pos", Vec3{0.0f, 0.0f, 0.0f});
    out_spawn.car_pos = Vec3{car_xz.x,
                             terrain.heightfield().sample(car_xz.x, car_xz.y) + 1.0f,
                             car_xz.y};
    out_spawn.car_yaw = cfg.get_f32("spawn.car_yaw_deg", 0.0f) * kDegToRad;

    const Quat car_rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, out_spawn.car_yaw);
    const Vec3 beside_door = out_spawn.car_pos + rotate(car_rot, Vec3{-2.6f, 0.0f, -0.3f});
    const Vec3 player_xz = cfg.get_vec3("spawn.player_pos",
                                        Vec3{beside_door.x, beside_door.z, 0.0f});
    out_spawn.player_pos = Vec3{player_xz.x,
                                terrain.heightfield().sample(player_xz.x, player_xz.y),
                                player_xz.y};

    const Vec3 to_car = out_spawn.car_pos - out_spawn.player_pos;
    const f32 default_yaw = std::atan2(to_car.x, -to_car.z) * kRadToDeg;
    out_spawn.player_yaw = cfg.get_f32("spawn.player_yaw_deg", default_yaw) * kDegToRad;

    log_info("zone: loaded %s | %u entities | %u static tris | %u pickups", path.c_str(),
             entity_count, phys.statics().tri_count(),
             out_pickups ? out_pickups->count : 0);
    return true;
}

bool zone_reload(std::string_view zone_dir, Arena& arena, Arena& scratch, World& world,
                 PhysWorld& phys, const Terrain& terrain, ZonePickups* out_pickups)
{
    ArenaScope scope(scratch);

    FixedString<256> path;
    path.format("%.*s/zone.cfg", static_cast<int>(zone_dir.size()), zone_dir.data());
    const fs::FileData file = fs::read_entire_file(scratch, path.view());
    if (!file.valid()) {
        return false;
    }

    Config cfg;
    if (!cfg.parse(scratch, file.text())) {
        return false;
    }

    Pool<Entity>& pool = world.entities();
    for (u32 idx = 0; idx < pool.capacity(); idx++) {
        Entity* e = pool.at(idx);
        if (e && e->body != kNoEntityBody) {
            pickup_body_destroy(phys, *e);
        }
    }
    world.clear();
    phys.statics_reserve(arena, kZoneMaxStaticTris);

    const u32 count = spawn_from_config(world, phys, scratch, terrain, cfg, nullptr,
                                        out_pickups);
    phys.statics_build(arena);

    log_info("zone: reloaded %s | %u entities", path.c_str(), count);
    return true;
}

void zone_add_entity_collision(const World& world, PhysWorld& phys, Arena& scratch)
{
    const Pool<Entity>& pool = world.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || !(e->flags & kEntityFlagCollides) || e->mesh_name.empty()) {
            continue;
        }
        if (e->kind == EntityKind::Tree) {
            add_tree_collision(phys, scratch, e->aux_kind, e->pos, e->rot, e->scale);
        } else if (e->kind == EntityKind::Building || e->kind == EntityKind::StaticMesh) {
            add_mesh_collision(phys, scratch, e->mesh_name.view(), e->kind, e->pos, e->rot, e->scale);
        }
    }
}

bool zone_out_of_bounds(const Heightfield& hf, Vec3 p)
{
    if (!hf.valid()) {
        return false;
    }
    const Aabb box = hf.bounds();
    return p.x < box.min.x - kZoneEscapeMarginXZ || p.x > box.max.x + kZoneEscapeMarginXZ
        || p.z < box.min.z - kZoneEscapeMarginXZ || p.z > box.max.z + kZoneEscapeMarginXZ
        || p.y < hf.min_height() - kZoneEscapeMarginY || p.y > hf.max_height() + kZoneEscapeMarginY
        || p.x != p.x || p.y != p.y || p.z != p.z;
}

void zone_gravity(const World& world, GravityField& out)
{
    out.clear();
    const Pool<Entity>& pool = world.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::Gravity) {
            continue;
        }
        GravityVolume volume;
        volume.pos = e->pos;
        volume.rot = e->rot;
        volume.half = e->half;
        volume.falloff = e->scale;
        volume.strength = e->aux_value;
        volume.shape = (e->aux_kind & 0xFu) == 1 ? GravityShape::Sphere : GravityShape::Box;
        const u32 mode = e->aux_kind >> 4;
        volume.mode = mode == 1 ? GravityMode::Curl
                    : mode == 2 ? GravityMode::Point
                                : GravityMode::Directional;
        volume.sector = static_cast<f32>(e->aux_data ? e->aux_data : 90u) * kDegToRad;
        out.add(volume);
    }
    static u32 warned = 0;
    if (out.dropped() > warned) {
        warned = out.dropped();
        log_warn("zone: %u gravity volumes over the limit of %u were ignored", out.dropped(),
                 GravityField::kMaxVolumes);
    }
}

bool zone_save(std::string_view zone_dir, Arena& scratch, const World& world,
               const PhysWorld& phys, const Terrain& terrain)
{
    ArenaScope scope(scratch);

    FixedString<256> path;
    path.format("%.*s/zone.cfg", static_cast<int>(zone_dir.size()), zone_dir.data());
    const fs::FileData existing = fs::read_entire_file(scratch, path.view());

    std::FILE* out = fs::open(path.view(), "wb");
    if (!out) {
        log_warn("zone: could not open %s for writing", path.c_str());
        return false;
    }

    u32 written = 0;
    bool emitted = false;
    bool in_entities = false;

    std::string_view text = existing.valid() ? existing.text() : std::string_view{};
    while (!text.empty()) {
        const std::size_t newline = text.find('\n');
        std::string_view line = newline == std::string_view::npos ? text
                                                                  : text.substr(0, newline);
        text = newline == std::string_view::npos ? std::string_view{}
                                                 : text.substr(newline + 1);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        const std::string_view trimmed = trim_left(line);
        if (!trimmed.empty() && trimmed.front() == '[') {
            in_entities = trimmed.substr(0, 10) == "[entities]";
            std::fprintf(out, "%.*s\n", static_cast<int>(line.size()), line.data());
            if (in_entities) {
                write_entities(out, world, phys, terrain, written);
                emitted = true;
            }
            continue;
        }
        if (in_entities && is_entity_key(line_key(line))) {
            continue;
        }
        std::fprintf(out, "%.*s\n", static_cast<int>(line.size()), line.data());
    }

    if (!emitted) {
        std::fprintf(out, "\n[entities]\n");
        write_entities(out, world, phys, terrain, written);
    }

    std::fclose(out);
    log_info("zone: saved %s | %u entities", path.c_str(), written);
    return true;
}

}
