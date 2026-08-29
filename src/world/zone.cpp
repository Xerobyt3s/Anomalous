#include "world/zone.h"
#include "assets/mesh_data.h"
#include "core/arena.h"
#include "core/config.h"
#include "core/log.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "platform/filesystem.h"
#include "world/entity.h"
#include "world/terrain.h"

#include <charconv>

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

    world.spawn(kind, pos, rot, scale, mesh_name, kEntityFlagCollides);
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

void spawn_tower(World& world, PhysWorld& phys, Arena& scratch, const Terrain& terrain,
                 std::string_view line, ZoneSpawn& out_spawn)
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

    out_spawn.tower_present = true;
    out_spawn.tower_x = x;
    out_spawn.tower_z = z;
    out_spawn.tower_yaw_deg = yaw_deg;
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

} // namespace

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

    out_spawn.tower_present = false;
    if (out_pickups) {
        out_pickups->count = 0;
    }

    u32 entity_count = 0;
    for (const Config::Entry& entry : cfg.entries()) {
        if (entry.key == "entities.spawn") {
            spawn_entity(world, phys, scratch, terrain, entry.value);
            entity_count++;
        } else if (entry.key == "entities.trigger") {
            spawn_trigger(world, terrain, entry.value);
            entity_count++;
        } else if (entry.key == "entities.tower") {
            spawn_tower(world, phys, scratch, terrain, entry.value, out_spawn);
            entity_count++;
        } else if (entry.key == "entities.pickup") {
            collect_pickup(entry.value, out_pickups);
        }
    }
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

} // namespace anom
