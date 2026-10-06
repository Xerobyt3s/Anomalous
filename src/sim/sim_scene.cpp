#include "sim/sim.h"
#include "core/log.h"
#include "math/glm_bridge.h"
#include "world/scenes.h"

#include <algorithm>
#include <vector>

namespace anom {
namespace {

constexpr Vec3 kParkedCar{0.0f, -2000.0f, 0.0f};
constexpr f32 kBenchReach = 1.6f;
constexpr f32 kBenchFillTime = 0.12f;

}

const ZoneScene& Sim::scene() const
{
    return spawn_.scene;
}

bool Sim::has_car() const
{
    return spawn_.scene.car;
}

bool Sim::owns(const PlayerSlot& s) const
{
    return role_ == SimRole::Client ? s.id == local_ : !s.remote;
}

std::vector<const Entity*> Sim::scene_entities(EntityKind kind) const
{
    std::vector<const Entity*> out;
    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        if (e && e->kind == kind) {
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const Entity* a, const Entity* b) { return a->aux_kind < b->aux_kind; });
    return out;
}

const Entity* Sim::spawn_entity_for(PlayerId id) const
{
    const std::vector<const Entity*> points = scene_entities(EntityKind::SpawnPoint);
    if (points.empty()) {
        return nullptr;
    }
    return points[slot_index(id) % points.size()];
}

const Entity* Sim::bench_for(PlayerId id) const
{
    const std::vector<const Entity*> benches = scene_entities(EntityKind::Bench);
    if (benches.empty()) {
        return nullptr;
    }
    return benches[slot_index(id) % benches.size()];
}

f32 Sim::spawn_yaw(PlayerId id) const
{
    if (const Entity* point = spawn_entity_for(id)) {
        return -quat_yaw(point->rot);
    }
    return spawn_.player_yaw;
}

void Sim::park_car()
{
    for (PlayerSlot& s : slots_) {
        if (s.active && s.player.state() != PlayerState::OnFoot) {
            s.player.eject(phys_, vehicle_);
        }
    }
    if (RigidBody* body = phys_.body(vehicle_.body())) {
        body->pos = kParkedCar;
        body->prev_pos = kParkedCar;
        body->vel = Vec3{};
        body->angular_vel = Vec3{};
        body->force_accum = Vec3{};
        body->torque_accum = Vec3{};
    }
    vehicle_.set_input(VehicleInput{});
    vehicle_.reset_contacts();
    car_parked_ = true;
}

void Sim::apply_scene()
{
    rules_ = base_rules_;
    if (spawn_.scene.kind == SceneKind::Arena) {
        rules_.arena = true;
        rules_.friendlyFire = true;
    }
    roster_.reset();
    if (!has_car()) {
        park_car();
    } else if (car_parked_) {
        car_parked_ = false;
        reset_car();
    }
    for (PlayerSlot& s : slots_) {
        s.bench_time = 0.0f;
        if (s.active && owns(s) && s.zombie_of == kNoPlayer) {
            if (rules_.arena) {
                gameplay_.arenaKit(s.gun);
            }
        }
    }
}

bool Sim::switch_scene(i32 index)
{
    if (role_ == SimRole::Client || index < 0 || static_cast<u32>(index) >= scenes().size()) {
        return false;
    }
    for (PlayerSlot& s : slots_) {
        if (s.active && s.player.state() != PlayerState::OnFoot) {
            s.player.eject(phys_, vehicle_);
        }
    }
    travel_target_ = -1;
    travel_jump_ = -1.0f;
    if (!load_zone(scenes()[static_cast<u32>(index)].zone_dir)) {
        return false;
    }
    for (PlayerSlot& s : slots_) {
        if (s.active && s.zombie_of == kNoPlayer) {
            place_player(s, spawn_point(s.id), spawn_yaw(s.id));
        }
    }
    log_info("scene: %.*s", static_cast<int>(scenes()[static_cast<u32>(index)].name.size()),
             scenes()[static_cast<u32>(index)].name.data());
    return true;
}

void Sim::respawn_owned(PlayerId id)
{
    PlayerSlot* s = slot(id);
    if (!s || !owns(*s)) {
        return;
    }
    s->player.init(spawn_point(id), spawn_yaw(id));
    if (rules_.arena) {
        gameplay_.arenaKit(s->gun);
    }
}

void Sim::tick_bench(PlayerSlot& s, f32 dt)
{
    const Entity* bench = bench_for(s.id);
    const MoveState& m = s.player.movement().state();
    const bool near = bench && s.player.state() == PlayerState::OnFoot && !roster_.downed(s.id)
                   && length(Vec3{bench->pos.x - m.pos.x, 0.0f, bench->pos.z - m.pos.z}) < kBenchReach;
    s.at_bench = near;
    if (!near || !s.use_down) {
        s.bench_time = 0.0f;
        return;
    }
    s.bench_time += dt;
    while (s.bench_time >= kBenchFillTime) {
        s.bench_time -= kBenchFillTime;
        bool filled = false;
        for (ghost::game::Speedloader& loader : s.gun.speedloaders.loaders) {
            for (std::optional<ghost::game::Round>& slot : loader.slots) {
                if (!filled && !slot && s.gun.pouch.take(ghost::game::kPlainElement)) {
                    slot = ghost::game::Round{};
                    filled = true;
                }
            }
        }
        if (!filled) {
            s.bench_time = 0.0f;
            return;
        }
    }
}

}
