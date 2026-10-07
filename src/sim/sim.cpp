#include "sim/sim.h"
#include "world/scenes.h"
#include "world/pickup_body.h"
#include "core/log.h"
#include "game/ballistics/surface.h"
#include "engine/assets/asset_path.h"
#include "game/ghosts/ghost_poses.h"
#include "math/glm_bridge.h"
#include "physics/heightfield.h"
#include "terminal/program.h"
#include "world/destination.h"
#include "vehicle/surface.h"

#include <cmath>
#include <cstring>
#include <variant>

namespace anom {
namespace {
constexpr f32 kFallGuardMargin = 15.0f;
constexpr u64 kIslandsArenaBytes = 48ull * 1024ull * 1024ull;
constexpr u64 kZoneArenaBytes = 64ull * 1024ull * 1024ull;
constexpr f32 kIslandsSettle = 0.3f;
constexpr f32 kSpoolSpeedKmh = 90.0f;
constexpr f32 kSpoolSeconds = 12.0f;
constexpr f32 kSpoolBleedRate = 0.14f;
constexpr f32 kJumpCollapse = 0.85f;
constexpr f32 kJumpArrive = 1.10f;
constexpr f32 kJumpCarrySpeed = 0.75f;
constexpr Vec3 kTowerPortLocal{0.0f, 1.35f, 0.62f};
constexpr f32 kHandsEyeHeight = 1.38f;
constexpr f32 kRefuelNotice = 0.005f;
constexpr f32 kHornMinCharge = 0.05f;
constexpr f32 kSlotSpacing = 1.2f;
constexpr u32 kCableNearPickups = 12;
constexpr f32 kDebugGhostAhead = 8.0f;
constexpr f32 kDummyAhead = 4.0f;

Vec3 body_point(const RigidBody& body, Vec3 local)
{
    return body.pos + rotate(body.rot, local);
}

void hash_bytes(u64& h, const void* data, size_t size)
{
    const u8* bytes = static_cast<const u8*>(data);
    for (size_t i = 0; i < size; i++) {
        h = (h ^ bytes[i]) * 1099511628211ull;
    }
}

}

static u32 tank_total(const SynthBay& synth)
{
    u32 total = 0;
    for (const u32 dose : synth.tank) {
        total += dose;
    }
    return total;
}

static CarSnapshot snapshot_car(const CarSys& sys, const Drivetrain& train)
{
    CarSnapshot s{};
    s.door_target[0] = sys.door_target[0];
    s.door_target[1] = sys.door_target[1];
    s.hood_target = sys.hood_target;
    s.trunk_target = sys.trunk_target;
    s.engine_on = sys.engine_on;
    s.impact_serial = sys.impact_serial;
    for (u32 i = 0; i < PART_COUNT; i++) {
        s.installed[i] = sys.parts[i].installed;
    }
    s.handbrake_latched = sys.handbrake_latched;
    s.headlight_switch = sys.headlight_switch;
    s.wiper_mode = sys.wiper_mode;
    s.fuel_cap_open = sys.fuel_cap_open;
    s.key_inserted = sys.key_inserted;
    s.fuel = sys.fluids.fuel;
    s.oil = sys.fluids.oil;
    s.cargo_count = sys.cargo_count();
    s.floppy_disk = sys.floppy_disk;
    s.tape_inserted = sys.tape_inserted;
    s.deck_play = sys.deck_play;
    s.gear = train.gear;
    s.tank_total = tank_total(sys.synth);
    return s;
}

bool Sim::init(Arena& perm, Arena& scratch, std::string_view zone_dir)
{
    perm_ = &perm;
    scratch_ = &scratch;
    zone_dir_.assign(zone_dir);

    world_.init(perm);
    phys_.init(perm, &terrain_.heightfield());
    phys_.set_gravity_field(&gravity_);
    phys_.set_jolt(&jolt_);

    if (!islands_arena_.reserve(kIslandsArenaBytes)) {
        log_error("sim: islands arena reserve failed");
        return false;
    }
    if (!zone_arena_.reserve(kZoneArenaBytes)) {
        log_error("sim: zone arena reserve failed");
        return false;
    }
    if (!zone_load(zone_dir, zone_arena_, scratch, world_, phys_, terrain_, spawn_, &pickups_)) {
        log_error("sim: zone load failed");
        return false;
    }
    zone_gravity(world_, gravity_);
    rebuild_islands();

    const f32 tx = spawn_.tower_present ? spawn_.tower_x : 258.0f;
    const f32 tz = spawn_.tower_present ? spawn_.tower_z : 82.0f;
    const f32 tyaw = spawn_.tower_present ? spawn_.tower_yaw_deg : 20.0f;
    tower_pos_ = Vec3{tx, terrain_.heightfield().sample(tx, tz), tz};
    tower_rot_ = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, tyaw * kDegToRad);

    if (!vehicle_.init(phys_, scratch, "assets/cars/mustang.cfg", spawn_.car_pos, spawn_.car_yaw)) {
        return false;
    }

    rules_ = ghost::game::loadPlayerRules(ghost::engine::assetPath("data"));
    base_rules_ = rules_;
    ghost::game::GameplayHooks hooks;
    hooks.push = [this](PlayerId id, const glm::vec3& dv) { push_player(id, dv); };
    hooks.selfCast = [this](PlayerId id, const ghost::game::ElementDef& def, const glm::vec3& dir) {
        return self_cast(id, def, dir);
    };
    hooks.raiseZombie = [this](PlayerId player) { return raise_zombie(player); };
    hooks.gravity = [this](const glm::vec3& at) { return to_glm(gravity_.gravity_at(from_glm(at))); };
    hooks.wind = [this](const glm::vec3&) { return to_glm(weather_.wind_velocity()); };
    gameplay_.setHooks(std::move(hooks));
    spawn_zone_ghosts();
    for (u32 i = 0; i < kMaxSlots; i++) {
        slots_[i] = PlayerSlot{};
        slots_[i].id = slot_id(i);
    }
    add_player("Player");
    apply_scene();
    carsys_.init();
    weather_.init(20260718ull);
    tapes_.init();
    boxes_.init(scratch, "assets/cars/mustang_interact.cfg");
    interact_spawn_zone_pickups(world_, phys_, terrain_, pickups_);
    stock_fresh_tanks();

    disks_.init(perm);
    terminal_.init(perm, disks_);
    terminal_.mapdata().init(terrain_.heightfield());

    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        if (e && (e->flags & kEntityFlagTower)) {
            tower_pos_ = e->pos;
            tower_rot_ = e->rot;
            break;
        }
    }
    photo_.resize(kPhotoBytes);
    return true;
}

void Sim::reset_car()
{
    vehicle_.teleport(phys_, spawn_.car_pos, spawn_.car_yaw);
}

bool Sim::take_terrain_dirty()
{
    const bool dirty = terrain_dirty_;
    terrain_dirty_ = false;
    return dirty;
}

void Sim::poll_hot_reload(Arena& scratch, f64 now)
{
    vehicle_.poll_config_reload(phys_, scratch);
    boxes_.poll(scratch, now);
}

void Sim::queue_photo(const u8* rgb, PlayerId by)
{
    std::memcpy(photo_.data(), rgb, kPhotoBytes);
    photo_pending_ = true;
    photo_by_ = by;
}

void Sim::commit_photo()
{
    if (!photo_pending_) {
        return;
    }
    photo_pending_ = false;
    const bool saved = disks_.camera_capture(&terminal_.fs(), photo_.data());
    emit(ghost::game::PhotoTaken{saved, disks_.camera_exposures_left(&terminal_.fs()), photo_by_});
}

void Sim::apply_surface_grip()
{
    const RigidBody* body = phys_.body(vehicle_.body());
    if (!body) {
        return;
    }
    VehicleEffects& fx = vehicle_.effects();
    const f32 wetness = weather_.wetness();
    const f32 grass = vehicle_.config().tire_grass_grip;
    f32 road_sum = 0.0f;
    for (u32 i = 0; i < kWheelCount; i++) {
        const Wheel& w = vehicle_.wheel(i);
        const Vec3 at = w.grounded ? w.contact_point : body->pos;
        const f32 road = terrain_.road_amount(at.x, at.z);
        fx.surface_road[i] = road;
        fx.tire_grip_mul[i] *= surface_grip_mul(road, wetness, grass);
        road_sum += road;
    }
    fx.rolling_resist_mul = surface_rolling_mul(road_sum * 0.25f, vehicle_.config().offroad_rolling_mul);
}

bool own_event(const ghost::game::GameEvent& e, PlayerId local)
{
    using namespace ghost::game;
    if (const auto* shot = std::get_if<ShotFired>(&e)) {
        return shot->player == local;
    }
    if (const auto* cast = std::get_if<SelfCast>(&e)) {
        return cast->player == local;
    }
    return std::holds_alternative<DryFired>(e) || std::holds_alternative<HammerCocked>(e) ||
           std::holds_alternative<CylinderOpened>(e) || std::holds_alternative<CylinderClosed>(e) ||
           std::holds_alternative<ChambersEjected>(e) || std::holds_alternative<SpeedloaderUsed>(e) ||
           std::holds_alternative<RoundLoaded>(e) || std::holds_alternative<RoundPickedUp>(e) ||
           std::holds_alternative<PlayerHit>(e) || std::holds_alternative<GunHolstered>(e);
}

bool echo_of_mine(const ghost::game::GameEvent& e, PlayerId local)
{
    if (const auto* shot = std::get_if<ghost::game::ShotFired>(&e)) {
        return shot->player == local;
    }
    if (const auto* cast = std::get_if<ghost::game::SelfCast>(&e)) {
        return cast->player == local;
    }
    return false;
}

void Sim::spawn_debug_ghost(PlayerId id, const PlayerCommand& cmd)
{
    const PlayerSlot* s = slot(id);
    const auto& types = gameplay_.ghostData().types;
    if (!s || cmd.spawn_ghost < 0 || static_cast<size_t>(cmd.spawn_ghost) >= types.size()) {
        return;
    }
    const ghost::game::GhostDef& def = types[static_cast<size_t>(cmd.spawn_ghost)];
    const MoveState& m = s->player.movement().state();
    const Vec3 ahead = frame_forward(m.frame, m.yaw);
    const Vec3 up = s->player.up();
    const Vec3 side = normalize(cross(ahead, up));
    const bool mimic = def.behavior == ghost::game::GhostBehavior::Mimic;
    const f32 reach = mimic ? 3.0f : def.behavior == ghost::game::GhostBehavior::BallLightning ? 7.0f : kDebugGhostAhead;
    const f32 lift = mimic ? 0.6f : 1.5f;
    const i32 count = cmd.spawn_ghost_count < 1 ? 1 : (cmd.spawn_ghost_count > 10 ? 10 : cmd.spawn_ghost_count);
    for (i32 n = 0; n < count; n++) {
        const f32 angle = static_cast<f32>(n) * 2.4f;
        const Vec3 spread = n == 0 ? Vec3{} : side * std::cos(angle) + ahead * std::sin(angle);
        const Vec3 at = s->player.pos() + ahead * reach + spread + up * lift;
        gameplay_.spawnGhost(def.name, to_glm(at));
    }
}

bool Sim::wears_cowboy(PlayerId id) const
{
    const PlayerSlot* s = slot(id);
    if (!s) {
        return false;
    }
    if (s->cowboy == 1) {
        return true;
    }
    if (s->cowboy == 2 || !rules_.arena) {
        return false;
    }
    const auto leader = ghost::game::dominantLeader(roster_.entries(), rules_.arenaHatLead);
    return leader && *leader == id;
}

void Sim::pose_specimen()
{
    const auto& types = gameplay_.ghostData().types;
    if (specimen_.id == 0 || specimen_.type < 0 || static_cast<size_t>(specimen_.type) >= types.size()) {
        return;
    }
    gameplay_.poseGhost(specimen_.id, ghost::game::specimenPose(types[static_cast<size_t>(specimen_.type)], specimen_.pose,
                                                                to_glm(specimen_.at), to_glm(specimen_.forward),
                                                                to_glm(specimen_.up), specimen_.size));
}

void Sim::apply_host_debug(PlayerId id, const PlayerCommand& cmd)
{
    if (cmd.cowboy_target >= 0) {
        if (PlayerSlot* target = slot(static_cast<PlayerId>(cmd.cowboy_target))) {
            target->cowboy = cmd.cowboy_mode <= 2 ? cmd.cowboy_mode : 0;
        }
    }
    const auto& types = gameplay_.ghostData().types;
    if (specimen_.id != 0 && !gameplay_.ghosts().find(specimen_.id)) {
        specimen_.id = 0;
    }
    if (cmd.specimen_remove && specimen_.id != 0) {
        gameplay_.removeGhost(specimen_.id);
        specimen_.id = 0;
    }
    bool changed = false;
    if (cmd.specimen_type >= 0 && static_cast<size_t>(cmd.specimen_type) < types.size()) {
        if (cmd.specimen_type != specimen_.type && specimen_.id != 0) {
            gameplay_.removeGhost(specimen_.id);
            specimen_.id = gameplay_.spawnGhost(types[static_cast<size_t>(cmd.specimen_type)].name,
                                                to_glm(specimen_.at + specimen_.up));
        }
        specimen_.type = cmd.specimen_type;
        specimen_.pose = cmd.specimen_pose;
        specimen_.size = cmd.specimen_size;
        changed = true;
    }
    const PlayerSlot* s = slot(id);
    if (cmd.specimen_place && s && static_cast<size_t>(specimen_.type) < types.size()) {
        const MoveState& m = s->player.movement().state();
        specimen_.up = s->player.up();
        specimen_.forward = frame_forward(m.frame, m.yaw);
        specimen_.at = s->player.pos() + specimen_.forward * 3.0f;
        if (specimen_.id == 0) {
            specimen_.id = gameplay_.spawnGhost(types[static_cast<size_t>(specimen_.type)].name,
                                                to_glm(specimen_.at + specimen_.up));
        }
        changed = true;
    }
    if (changed) {
        pose_specimen();
    }
}

void Sim::sync_pickup_transforms()
{
    Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        Entity* e = pool.at(idx);
        if (!e || e->kind != EntityKind::PartPickup || e->body == kNoEntityBody) {
            continue;
        }
        PickupState body;
        if (!pickup_body_state(phys_, *e, body)) {
            continue;
        }
        const ItemKind kind = static_cast<ItemKind>(e->aux_kind);
        e->rot = body.rot * item_cargo_rot(kind);
        e->pos = body.pos - rotate(e->rot, item_mesh_center(kind));
    }
}

PlayerSlot* Sim::slot(PlayerId id)
{
    return slot_index(id) < kMaxSlots && slots_[slot_index(id)].active ? &slots_[slot_index(id)] : nullptr;
}

const PlayerSlot* Sim::slot(PlayerId id) const
{
    return slot_index(id) < kMaxSlots && slots_[slot_index(id)].active ? &slots_[slot_index(id)] : nullptr;
}

PlayerId Sim::driver() const
{
    if (role_ == SimRole::Client) {
        const PlayerSlot& mine = slots_[slot_index(local_)];
        if (mine.active && mine.player.state() != PlayerState::OnFoot && mine.player.seat() == 0) {
            return local_;
        }
        return mirror_driver_ == local_ ? kNoPlayer : mirror_driver_;
    }
    if (const PlayerSlot* owner = slot(seat_owners_[0])) {
        if (owner->player.state() != PlayerState::OnFoot && owner->player.seat() == 0) {
            return seat_owners_[0];
        }
    }
    for (const PlayerSlot& s : slots_) {
        if (s.active && s.player.state() != PlayerState::OnFoot && s.player.seat() == 0) {
            return s.id;
        }
    }
    return kNoPlayer;
}

u32 Sim::seats_taken_for(PlayerId id) const
{
    u32 mask = 0;
    for (const PlayerSlot& s : slots_) {
        if (s.active && s.id != id && s.player.state() != PlayerState::OnFoot) {
            mask |= 1u << s.player.seat();
        }
    }
    for (u32 k = 0; k < kMaxSeats; k++) {
        if (seat_owners_[k] != kNoPlayer && seat_owners_[k] != id) {
            mask |= 1u << k;
        }
    }
    return mask;
}

bool Sim::seat_blocked(u32 seat) const
{
    const VehicleConfig& cfg = vehicle_.config();
    if (seat >= cfg.seat_count) {
        return true;
    }
    const PartKind blocker = cfg.seats[seat].blocked_by;
    return blocker != PART_COUNT && carsys_.parts[blocker].installed;
}

const PlayerSlot* Sim::holding(ItemKind kind) const
{
    for (const PlayerSlot& s : slots_) {
        if (s.active && s.interact.hands().kind == kind) {
            return &s;
        }
    }
    return nullptr;
}

Vec3 Sim::spawn_point(PlayerId id) const
{
    if (const Entity* point = spawn_entity_for(id)) {
        return point->pos;
    }
    const Vec3 right = rotate(quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -spawn_.player_yaw), Vec3{1.0f, 0.0f, 0.0f});
    const f32 offset = static_cast<f32>(id) * kSlotSpacing;
    return spawn_.player_pos + right * offset;
}

void Sim::place_player(PlayerSlot& s, Vec3 feet, f32 yaw)
{
    s.player.set_character(slot_index(s.id));
    s.player.init(feet, yaw);
    if (role_ == SimRole::Host && s.remote) {
        emit(ghost::game::PlayerPlaced{s.id, to_glm(feet), yaw});
    }
}

PlayerId Sim::add_player(std::string_view name, bool remote)
{
    for (u32 i = 0; i < kMaxPlayers; i++) {
        PlayerSlot& s = slots_[i];
        if (s.active) {
            continue;
        }
        const PlayerId id = s.id;
        s = PlayerSlot{};
        s.id = id;
        s.active = true;
        s.name.assign(name);
        s.remote = remote;
        s.color = kSlotColors[id];
        s.interact.init();
        kit_gun(s.gun);
        place_player(s, spawn_point(id), spawn_yaw(id));
        commands_[slot_index(id)] = PlayerCommand{};
        roster_.add(id);
        return id;
    }
    return kNoPlayer;
}

void Sim::remove_player(PlayerId id)
{
    PlayerSlot* s = slot(id);
    if (!s) {
        return;
    }
    if (s->interact.hands().kind != ITEM_NONE) {
        s->interact.drop(world_, phys_, hands_eye(id), s->view_dir, 0.0f);
    }
    if (s->interact.cable_drag() >= 0) {
        drop_cable(static_cast<u32>(s->interact.cable_drag()));
    }
    if (terminal_user_ == id) {
        terminal_user_ = kNoPlayer;
    }
    jolt_.characterDestroy(static_cast<int>(slot_index(id)));
    roster_.remove(id);
    *s = PlayerSlot{};
    s->id = id;
}

PlayerId Sim::spawn_dummy()
{
    const PlayerId id = add_player("Dummy");
    PlayerSlot* s = slot(id);
    if (!s) {
        return kNoPlayer;
    }
    s->dummy = true;
    s->name.format("Dummy %u", static_cast<u32>(id));
    if (const PlayerSlot* near = nearest_human(spawn_.player_pos)) {
        const Player& p = near->player;
        const Vec3 ahead = frame_forward(p.movement().state().frame, p.yaw());
        place_player(*s, p.pos() + ahead * kDummyAhead + p.up() * 0.2f, f_wrap_angle(p.yaw() + kPi));
    }
    log_info("sim: %s joins", s->name.c_str());
    return id;
}

const PlayerSlot* Sim::nearest_human(Vec3 from) const
{
    const PlayerSlot* best = nullptr;
    f32 best_d = 1e30f;
    for (const PlayerSlot& s : slots_) {
        if (!s.active || s.dummy) {
            continue;
        }
        const f32 d = length_sq(s.player.pos() - from);
        if (d < best_d) {
            best_d = d;
            best = &s;
        }
    }
    return best;
}

void Sim::reset_player(PlayerId id)
{
    PlayerSlot* s = slot(id);
    if (!s) {
        return;
    }
    place_player(*s, spawn_point(id), spawn_yaw(id));
}

Vec3 Sim::hands_eye(PlayerId id) const
{
    const Player& p = slots_[slot_index(id)].player;
    return p.pos() + p.up() * kHandsEyeHeight;
}

Vec3 Sim::hands_pos(PlayerId id) const
{
    const PlayerSlot& s = slots_[slot_index(id)];
    const bool driving = s.player.driving();
    const f32 reach = driving ? 0.40f : 0.62f;
    const f32 drop = driving ? 0.22f : 0.34f;
    Vec3 right = cross(s.view_dir, s.player.up());
    right = length_sq(right) > 1e-6f ? normalize(right) : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 pos = s.view_origin + s.view_dir * reach + right * (driving ? 0.20f : 0.30f);
    pos.y -= drop;
    return pos;
}

Vec3 Sim::tower_port_pos() const
{
    return tower_pos_ + rotate(tower_rot_, kTowerPortLocal);
}

const Entity* Sim::find_pickup(ItemKind kind) const
{
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::PartPickup && static_cast<ItemKind>(e->aux_kind) == kind) {
            return e;
        }
    }
    return nullptr;
}

const Entity* Sim::find_pickup(ItemKind kind, i32 aux) const
{
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::PartPickup && static_cast<ItemKind>(e->aux_kind) == kind
            && static_cast<i32>(e->aux_data) == aux) {
            return e;
        }
    }
    return nullptr;
}

void Sim::stock_fresh_tanks()
{
    if (role_ == SimRole::Client) {
        return;
    }
    const SynthTuning& tuning = vehicle_.config().synth;
    const ghost::game::AmmoData& ammo = gameplay_.ammo();
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        Entity* e = world_.entities().at(idx);
        if (!e || e->kind != EntityKind::PartPickup || static_cast<ItemKind>(e->aux_kind) != ITEM_TANK
            || carsys_.loose_tank(static_cast<i32>(e->aux_data))) {
            continue;
        }
        const i32 id = carsys_.claim_tank();
        LooseTank* tank = carsys_.loose_tank(id);
        if (!tank) {
            return;
        }
        u32 total = 0;
        for (u32 m = 0; m < kSynthMaterials && m < ammo.materials.size(); m++) {
            const u32 wanted = ammo.materials[m].propellant ? tuning.start_propellant : tuning.start_each;
            const u32 room = tuning.tank_capacity > total ? tuning.tank_capacity - total : 0;
            tank->doses[m] = static_cast<u16>(std::min(wanted, room));
            total += tank->doses[m];
        }
        e->aux_data = static_cast<u32>(id);
    }
}

void Sim::release_lost_stores()
{
    bool tanks[kLooseTanks] = {};
    bool printers[kLoosePrinters] = {};
    const auto keep = [&](ItemKind kind, i32 aux) {
        if (kind == ITEM_TANK && aux >= 1 && aux <= static_cast<i32>(kLooseTanks)) {
            tanks[aux - 1] = true;
        }
        if (kind == ITEM_PRINTER && aux >= 1 && aux <= static_cast<i32>(kLoosePrinters)) {
            printers[aux - 1] = true;
        }
    };
    const Pool<Entity>& pool = world_.entities();
    for (u32 idx : pool.live_indices()) {
        const Entity* e = pool.at(idx);
        if (e && e->kind == EntityKind::PartPickup) {
            keep(static_cast<ItemKind>(e->aux_kind), static_cast<i32>(e->aux_data));
        }
    }
    for (const PlayerSlot& s : slots_) {
        if (s.active) {
            keep(s.interact.hands().kind, s.interact.hands().aux);
        }
    }
    for (const CargoItem& cargo : carsys_.cargo) {
        if (cargo.used) {
            keep(cargo.item.kind, cargo.item.aux);
        }
    }
    for (u32 i = 0; i < kLooseTanks; i++) {
        carsys_.loose_tanks[i].used = carsys_.loose_tanks[i].used && tanks[i];
    }
    for (u32 i = 0; i < kLoosePrinters; i++) {
        carsys_.loose_printers[i].used = carsys_.loose_printers[i].used && printers[i];
    }
}

void Sim::gather_commands(std::span<const SlotCommand> commands, f32 dt)
{
    const bool client = role_ == SimRole::Client;
    for (PlayerSlot& s : slots_) {
        if (!s.active || (client && s.remote)) {
            continue;
        }
        PlayerCommand& cmd = commands_[slot_index(s.id)];
        if (s.zombie_of != kNoPlayer && !s.remote) {
            cmd = zombie_command(s, dt);
        } else if (s.dummy && !s.remote) {
            cmd = dummy_command(s, dt);
        } else {
            PlayerCommand idle;
            idle.gameplay = !s.remote || cmd.gameplay;
            idle.view_origin = s.view_origin;
            idle.view_dir = s.view_dir;
            idle.use_down = s.remote && cmd.use_down;
            if (s.remote) {
                idle.throttle = cmd.throttle;
                idle.reverse = cmd.reverse;
                idle.steer = cmd.steer;
                idle.handbrake = cmd.handbrake;
                idle.terminal_orbit = cmd.terminal_orbit;
                idle.terminal_zoom = cmd.terminal_zoom;
            }
            cmd = idle;
            for (const SlotCommand& in : commands) {
                if (in.id == s.id) {
                    cmd = in.cmd;
                }
            }
        }
        s.view_origin = cmd.view_origin;
        s.view_dir = length_sq(cmd.view_dir) > 1e-8f ? normalize(cmd.view_dir) : Vec3{0.0f, 0.0f, -1.0f};
        s.use_down = cmd.gameplay && cmd.use_down;
    }
}

void Sim::apply_debug(const PlayerCommand& cmd)
{
    if (cmd.recover) {
        vehicle_.recover(phys_);
    }
    if (cmd.reset_car) {
        reset_car();
    }
    if (!cmd.dummy_cycle && cmd.dummy_script < 0) {
        return;
    }
    PlayerSlot* dummy = nullptr;
    for (PlayerSlot& s : slots_) {
        if (s.active && s.dummy) {
            dummy = &s;
            break;
        }
    }
    if (!dummy) {
        dummy = slot(spawn_dummy());
        if (!dummy) {
            return;
        }
        if (cmd.dummy_script < 0) {
            return;
        }
    }
    const u32 count = static_cast<u32>(DummyScript::Count);
    const u32 next = cmd.dummy_script >= 0 ? static_cast<u32>(cmd.dummy_script) % count
                                           : (static_cast<u32>(dummy->script) + 1) % count;
    dummy->script = static_cast<DummyScript>(next);
    dummy->script_time = 0.0f;
    log_info("sim: %s %.*s", dummy->name.c_str(), static_cast<int>(dummy_script_name(dummy->script).size()),
             dummy_script_name(dummy->script).data());
}

void Sim::apply_car_controls()
{
    const PlayerId id = driver();
    PlayerSlot* s = slot(id);
    if (!s || s->player.state() != PlayerState::Driving) {
        VehicleInput parked;
        parked.handbrake = carsys_.handbrake_latched;
        vehicle_.set_input(parked);
        carsys_.horn_on = false;
        return;
    }
    const PlayerCommand& cmd = commands_[slot_index(id)];
    if (!cmd.gameplay || terminal_user_ == id) {
        vehicle_.set_input(VehicleInput{});
        carsys_.horn_on = false;
        return;
    }
    if (cmd.handbrake_toggle && vehicle_.planar_speed(phys_) < vehicle_.config().park_latch_speed) {
        carsys_.handbrake_latched = !carsys_.handbrake_latched;
    }
    if (cmd.wipers_cycle) {
        carsys_.wiper_mode = (carsys_.wiper_mode + 1) % 3;
    }
    if (cmd.ignition_tap) {
        if (carsys_.engine_on) {
            carsys_.stop_engine();
        } else if (!carsys_.key_inserted && s->interact.has_key()) {
            carsys_.key_inserted = true;
            s->interact.set_has_key(false);
        }
    }
    carsys_.horn_on = cmd.horn && carsys_.elec.battery_charge > kHornMinCharge;
    const bool handbrake = cmd.handbrake || carsys_.handbrake_latched;
    vehicle_.driver_input(phys_, cmd.throttle, cmd.reverse, cmd.steer, handbrake);
    if (cmd.headlights_toggle) {
        carsys_.headlight_switch = !carsys_.headlight_switch;
    }
    if (cmd.manual_toggle) {
        vehicle_.train().manual = !vehicle_.train().manual;
    }
    if (vehicle_.train().manual) {
        for (i32 i = 0; i < cmd.shift; i++) {
            drivetrain_request_shift(vehicle_.train(), 1);
        }
        for (i32 i = 0; i < -cmd.shift; i++) {
            drivetrain_request_shift(vehicle_.train(), -1);
        }
    }
}

void Sim::apply_hands(PlayerSlot& s, const PlayerCommand& cmd)
{
    if (!cmd.gameplay || terminal_user_ == s.id) {
        return;
    }
    Interact& interact = s.interact;
    if (s.player.driving()) {
        if (cmd.take_key && carsys_.key_inserted && !carsys_.engine_on && interact.action() == InteractAction::Crank) {
            carsys_.key_inserted = false;
            carsys_.crank_request = false;
            interact.set_has_key(true);
        }
        return;
    }
    if (s.player.state() != PlayerState::OnFoot) {
        return;
    }
    if (interact.cable_drag() >= 0) {
        if (cmd.stow_cable) {
            carsys_.cables[interact.cable_drag()].reset();
            interact.set_cable_drag(-1);
        }
        return;
    }
    if (interact.hands().kind == ITEM_NONE) {
        return;
    }
    if (cmd.throw_power >= 0.0f) {
        interact.drop(world_, phys_, hands_eye(s.id), s.view_dir, cmd.throw_power);
    }
    if (cmd.place_commit && interact.hands().kind != ITEM_NONE) {
        interact_spawn_pickup(world_, phys_, interact.hands(), cmd.place_pos, cmd.place_yaw, Vec3{});
        interact.hands().kind = ITEM_NONE;
    }
}

void Sim::apply_terminal_input(const PlayerCommand& cmd)
{
    for (u32 i = 0; i < cmd.terminal_char_count && i < kCommandMaxChars; i++) {
        terminal_.key_char(cmd.terminal_chars[i]);
    }
    for (u32 k = 0; k <= static_cast<u32>(TermKey::Quit); k++) {
        if (cmd.terminal_keys & (1u << k)) {
            terminal_.key(static_cast<TermKey>(k));
        }
    }
    if (cmd.terminal_leave) {
        terminal_user_ = kNoPlayer;
    }
}

void Sim::update_hands_and_gun(PlayerSlot& s, const PlayerCommand& cmd, PlayerCommand& player_cmd, bool took, f32 dt)
{
    Interact& interact = s.interact;
    const MoveState& m = s.player.movement().state();
    if (took) {
        s.take_pending = false;
    }
    if (interact.take_holster_request() && s.player.state() == PlayerState::OnFoot) {
        player_cmd.holster = player_cmd.holster || !m.holstered;
        s.take_pending = true;
    } else if (s.take_pending && !m.holstered) {
        s.take_pending = false;
    }
    const bool holding = interact.hands().kind != ITEM_NONE;
    if (cmd.holster && holding && m.holstered && s.lowering < 0.0f && s.player.state() == PlayerState::OnFoot) {
        s.lowering = 0.0f;
    }
    if (s.lowering < 0.0f) {
        return;
    }
    player_cmd.holster = false;
    if (!holding || s.player.state() != PlayerState::OnFoot) {
        s.lowering = -1.0f;
        return;
    }
    s.lowering += dt;
    if (s.lowering < s.player.movement().tuning().lower_item_time) {
        return;
    }
    s.lowering = -1.0f;
    if (role_ == SimRole::Client) {
        interact.hands().kind = ITEM_NONE;
    } else {
        Vec3 ahead = s.view_dir;
        ahead.y = f_min(ahead.y, -0.35f);
        interact.drop(world_, phys_, hands_eye(s.id), normalize(ahead), 0.0f);
    }
    player_cmd.holster = true;
}

void Sim::interact_slot(PlayerSlot& s, const PlayerCommand& cmd, PlayerCommand& player_cmd, f32 dt)
{
    player_cmd = cmd;
    player_cmd.interact = false;
    const bool busy = !cmd.gameplay || terminal_user_ == s.id;
    if (!busy) {
        const u32 taken = seats_taken_for(s.id);
        Interact& interact = s.interact;
        interact.set_tower(spawn_.scene.kind == SceneKind::Zone, tower_port_pos());
        InteractContext ctx;
        ctx.player = &s.player;
        ctx.veh = has_car() ? &vehicle_ : nullptr;
        ctx.sys = &carsys_;
        ctx.world = &world_;
        ctx.phys = &phys_;
        ctx.tapes = &tapes_;
        ctx.view_ray = Ray{s.view_origin, s.view_dir};
        const MoveState& hand_state = s.player.movement().state();
        const bool take_now = s.take_pending && hand_state.holster >= 1.0f;
        ctx.e_down = cmd.use_down || take_now;
        ctx.e_pressed = cmd.use_pressed || take_now;
        ctx.seats_taken = taken;
        ctx.preview = role_ == SimRole::Client;
        ctx.gun_drawn = s.player.movement().state().holster < 1.0f;
        ctx.is_driver = driver() == s.id;
        ctx.materials = static_cast<u32>(gameplay_.materials().total());
        interact.update(boxes_, ctx, dt);
        if (interact.take_terminal_request() && carsys_.computer_on && terminal_user_ == kNoPlayer) {
            terminal_user_ = s.id;
        }
        update_hands_and_gun(s, cmd, player_cmd, take_now, dt);
        if (interact.take_rounds_request() && role_ != SimRole::Client) {
            take_tray(s, interact.rounds_target());
        }
        if (interact.pour_request() && role_ != SimRole::Client) {
            fill_tank(interact.pour_target());
        }
        s.player.set_speed_mul(f_max(1.0f - item_mass(interact.hands().kind) * 0.012f, 0.6f));
        if (cmd.use_pressed) {
            if (interact.action() == InteractAction::EnterCar) {
                const VehicleConfig& cfg = vehicle_.config();
                for (u32 k = 0; k < cfg.seat_count; k++) {
                    if (cfg.seats[k].door_side == interact.target_side() && !(taken & (1u << k)) && !seat_blocked(k)) {
                        s.player.set_seat(k);
                        player_cmd.interact = true;
                    }
                }
            } else if (interact.action() == InteractAction::ExitCar) {
                s.player.set_exit_pref(interact.target_side() == 0 ? -1 : 1);
                player_cmd.interact = true;
            }
        }
    }
    if (busy || terminal_user_ == s.id) {
        player_cmd.move_x = 0.0f;
        player_cmd.move_z = 0.0f;
        player_cmd.jump = false;
        player_cmd.crawl = false;
        player_cmd.run = false;
        player_cmd.crouch = false;
        player_cmd.look_dx = cmd.gameplay ? cmd.look_dx : 0.0f;
        player_cmd.look_dy = cmd.gameplay ? cmd.look_dy : 0.0f;
    }
}

void Sim::tick_roster(f32 dt)
{
    std::array<ghost::game::RosterInput, kMaxSlots> inputs{};
    u32 count = 0;
    for (const PlayerSlot& s : slots_) {
        if (s.active) {
            inputs[count++] = ghost::game::RosterInput{s.id, to_glm(s.player.pos()), s.use_down};
        }
    }
    const size_t first = events_.size();
    roster_.tick(dt, std::span<const ghost::game::RosterInput>(inputs.data(), count), events_);
    for (size_t i = first; i < events_.size(); i++) {
        if (const auto* back = std::get_if<ghost::game::PlayerRespawned>(&events_[i])) {
            respawn_owned(back->player);
        }
    }
    bool wiped = false;
    for (size_t i = first; i < events_.size(); i++) {
        wiped = wiped || std::holds_alternative<ghost::game::PlayerDied>(events_[i]);
    }
    if (wiped) {
        for (PlayerSlot& s : slots_) {
            if (s.active && s.zombie_of != kNoPlayer) {
                jolt_.characterDestroy(static_cast<int>(slot_index(s.id)));
                const PlayerId id = s.id;
                s = PlayerSlot{};
                s.id = id;
            }
        }
        gameplay_.forgetPlayers();
    }
    for (PlayerSlot& s : slots_) {
        if (!s.active) {
            continue;
        }
        if (wiped) {
            reset_player(s.id);
        }
        if (const ghost::game::RosterEntry* entry = roster_.find(s.id)) {
            s.player.movement().set_vitals(entry->health, entry->sinceHurt, entry->downed);
        }
    }
}

void Sim::tick(const PlayerCommand& cmd, f32 dt)
{
    const SlotCommand one{0, cmd};
    tick(std::span<const SlotCommand>(&one, 1), dt);
}

void Sim::tick(std::span<const SlotCommand> commands, f32 dt)
{
    const bool client = role_ == SimRole::Client;
    const size_t first_event = events_.size();
    const ghost::game::EventList from_host = std::move(host_events_);
    host_events_.clear();
    if (client) {
        if (!snapshots_.empty()) {
            apply_snapshot(snapshots_.back());
            snapshots_.clear();
        }
        if (!term_mirrors_.empty()) {
            terminal_.adopt(term_mirrors_.back());
            term_mirrors_.clear();
        }
        host_events_ = from_host;
        apply_host_events();
        host_events_.clear();
    }
    apply_inbound();

    const CarSnapshot before = (!client && car_before_valid_) ? car_before_
                                                             : snapshot_car(carsys_, vehicle_.train());
    if (!client) {
        i32 wanted_scene = -1;
        for (const SlotCommand& in : commands) {
            if (in.id == local_ && in.cmd.scene >= 0) {
                wanted_scene = in.cmd.scene;
            }
        }
        if (wanted_scene >= 0) {
            switch_scene(wanted_scene);
        }
        for (const SlotCommand& in : commands) {
            if (slot(in.id)) {
                apply_debug(in.cmd);
                spawn_debug_ghost(in.id, in.cmd);
            }
            if (in.id == local_) {
                apply_host_debug(in.id, in.cmd);
            }
        }
        adopt_remote_bodies(commands);
    }
    gather_world_for_gameplay();
    gather_commands(commands, dt);

    if (terminal_user_ != kNoPlayer && !slot(terminal_user_)) {
        terminal_user_ = kNoPlayer;
    }
    if (!client || driver() == local_) {
        apply_car_controls();
    }
    if (!client) {
        for (PlayerSlot& s : slots_) {
            if (s.active) {
                apply_hands(s, commands_[slot_index(s.id)]);
            }
        }
        if (terminal_user_ != kNoPlayer) {
            apply_terminal_input(commands_[slot_index(terminal_user_)]);
        }
    }

    std::array<PlayerCommand, kMaxSlots> player_cmds{};
    if (!client) {
        carsys_.crank_request = false;
    }
    for (PlayerSlot& s : slots_) {
        if (!s.active || (client && s.remote)) {
            continue;
        }
        interact_slot(s, commands_[slot_index(s.id)], player_cmds[slot_index(s.id)], dt);
        if (commands_[slot_index(s.id)].crank && !client && driver() == s.id) {
            carsys_.crank_request = true;
        }
    }

    const size_t gun_first = events_.size();
    tick_guns(dt);
    const size_t gun_last = events_.size();
    if (client) {
        gameplay_.tickClient(dt, events_);
    } else {
        gameplay_.tickBeforePhysics(dt, events_);
    }

    zone_gravity(world_, gravity_);
    islands_.fill_gravity(gravity_);
    weather_.tick(dt);
    carsys_.rain_level = weather_.rain();
    if (has_car()) {
        carsys_.tick(vehicle_, phys_, dt);
        apply_surface_grip();
        vehicle_.tick(phys_, dt);
    } else {
        park_car();
    }
    phys_.tick(dt);
    if (!client) {
        gameplay_.tickAfterPhysics(dt, first_event, events_);
    }
    const f32 wind_grip = gameplay_.playerWindGrip();
    for (PlayerSlot& s : slots_) {
        if (!s.active || (client && s.remote)) {
            continue;
        }
        const bool interacting = s.use_down && s.player.state() == PlayerState::OnFoot && s.interact.action() == InteractAction::None;
        if (const auto taken = gameplay_.tickPickups(s.id, s.gun, interacting, dt, events_); taken && s.remote) {
            gives_out_.push_back({s.id, *taken});
        }
    }
    for (PlayerSlot& s : slots_) {
        if (!s.active || s.remote) {
            continue;
        }
        const u32 idx = slot_index(s.id);
        if (player_cmds[idx].interact && s.player.state() == PlayerState::OnFoot
            && ((seats_taken_for(s.id) & (1u << s.player.seat())) || seat_blocked(s.player.seat()))) {
            player_cmds[idx].interact = false;
        }
        player_cmds[idx].hands_busy = !s.gun.mechanism.state().isClosed();
        if (wind_grip > 0.0f) {
            const Vec3 body = s.player.pos() + s.player.up() * 0.9f;
            player_cmds[idx].wind = from_glm(gameplay_.airVelocityAt(to_glm(body)) * wind_grip);
        }
        s.player.tick(phys_, has_car() ? &vehicle_ : nullptr, player_cmds[idx], dt);
        const bool holstered = s.player.movement().state().holstered;
        if (holstered != s.was_holstered) {
            s.was_holstered = holstered;
            emit(ghost::game::GunHolstered{holstered});
        }
    }

    sync_pickup_transforms();
    update_cables(dt);
    if (client) {
        advance_client(dt);
        guard_against_falling();
        watch_islands(dt);
        ghost::game::EventList kept;
        for (size_t i = first_event; i < events_.size(); i++) {
            const bool mine = i >= gun_first && i < gun_last;
            if (mine || own_event(events_[i], local_)) {
                kept.push_back(events_[i]);
            }
        }
        events_.resize(first_event);
        events_.insert(events_.end(), kept.begin(), kept.end());
        for (const ghost::game::GameEvent& event : from_host) {
            if (!echo_of_mine(event, local_)) {
                events_.push_back(event);
            }
        }
        tick_count_++;
        return;
    }
    arbitrate_seat();
    tick_roster(dt);
    fell_new_zombies(first_event);
    commit_photo();
    update_terminal(dt);
    update_travel(dt);
    guard_against_falling();
    watch_islands(dt);
    emit_car_events(before);
    car_before_ = snapshot_car(carsys_, vehicle_.train());
    car_before_valid_ = true;
    tick_count_++;
}

void Sim::emit_car_events(const CarSnapshot& before)
{
    const RigidBody* body = phys_.body(vehicle_.body());
    if (!body) {
        return;
    }
    const Vec3 com = vehicle_.config().com_offset;
    for (u32 side = 0; side < 2; side++) {
        if (carsys_.door_target[side] != before.door_target[side]) {
            const Vec3 local{side == 0 ? -0.9f : 0.9f, 0.1f, -0.2f};
            emit(ghost::game::DoorMoved{static_cast<int>(side), carsys_.door_target[side],
                                        to_glm(body_point(*body, local - com))});
        }
    }
    if (carsys_.hood_target != before.hood_target) {
        emit(ghost::game::HoodMoved{carsys_.hood_target, to_glm(body_point(*body, car_layout().hood_point - com))});
    }
    if (carsys_.trunk_target != before.trunk_target) {
        emit(ghost::game::TrunkMoved{carsys_.trunk_target, to_glm(body_point(*body, car_layout().trunk_point - com))});
    }
    const glm::vec3 engine_at = to_glm(body_point(*body, car_layout().engine_point - com));
    if (carsys_.engine_on && !before.engine_on) {
        emit(ghost::game::EngineStarted{engine_at});
    }
    if (!carsys_.engine_on && before.engine_on) {
        emit(ghost::game::EngineStopped{carsys_.stall_notice > 0.0f, engine_at});
    }
    if (carsys_.impact_serial != before.impact_serial) {
        emit(ghost::game::CarImpact{carsys_.last_impact_severity, to_glm(body->pos)});
    }
    for (u32 i = 0; i < PART_COUNT; i++) {
        if (carsys_.parts[i].installed == before.installed[i]) {
            continue;
        }
        const glm::vec3 at = to_glm(body_point(*body, part_def(static_cast<PartKind>(i)).socket_pos - com));
        if (carsys_.parts[i].installed) {
            emit(ghost::game::PartInstalled{static_cast<int>(i), at});
        } else {
            emit(ghost::game::PartRemoved{static_cast<int>(i), at});
        }
    }
    const glm::vec3 dash_at = to_glm(body_point(*body, boxes_.box(IBOX_HANDBRAKE).center - com));
    const glm::vec3 cap_at = to_glm(body_point(*body, boxes_.box(IBOX_FUEL).center - com));
    if (carsys_.handbrake_latched != before.handbrake_latched) {
        emit(ghost::game::HandbrakeMoved{carsys_.handbrake_latched, dash_at});
    }
    if (carsys_.headlight_switch != before.headlight_switch) {
        emit(ghost::game::HeadlightsSwitched{carsys_.headlight_switch});
    }
    if (carsys_.wiper_mode != before.wiper_mode) {
        emit(ghost::game::WipersSwitched{carsys_.wiper_mode});
    }
    if (carsys_.fuel_cap_open != before.fuel_cap_open) {
        emit(ghost::game::FuelCapMoved{carsys_.fuel_cap_open, cap_at});
    }
    if (carsys_.fluids.fuel > before.fuel + kRefuelNotice) {
        emit(ghost::game::Refuelled{carsys_.fluids.fuel - before.fuel, cap_at});
    }
    if (carsys_.fluids.oil > before.oil + kRefuelNotice) {
        emit(ghost::game::OilFilled{engine_at});
    }
    if (carsys_.key_inserted != before.key_inserted) {
        emit(ghost::game::KeyMoved{carsys_.key_inserted});
    }
    if (carsys_.cargo_count() != before.cargo_count) {
        emit(ghost::game::CargoMoved{carsys_.cargo_count() > before.cargo_count,
                                     to_glm(body_point(*body, car_layout().trunk_point - com))});
    }
    if ((carsys_.floppy_disk >= 0) != (before.floppy_disk >= 0)) {
        emit(ghost::game::DiskMoved{carsys_.floppy_disk >= 0});
    }
    if ((carsys_.tape_inserted >= 0) != (before.tape_inserted >= 0)) {
        emit(ghost::game::TapeMoved{carsys_.tape_inserted >= 0});
    }
    if (carsys_.deck_play != before.deck_play) {
        emit(ghost::game::DeckPlay{carsys_.deck_play});
    }
    if (vehicle_.train().gear != before.gear) {
        emit(ghost::game::GearShifted{vehicle_.train().gear, vehicle_.train().manual});
    }
    const u32 tank_now = tank_total(carsys_.synth);
    if (tank_now > before.tank_total) {
        emit(ghost::game::TankFilled{static_cast<int>(tank_now - before.tank_total)});
    }
}

bool Sim::default_gravity_at(Vec3 p) const
{
    return gravity_.sample(p).presence < 0.01f;
}

void Sim::guard_against_falling()
{
    const f32 floor = terrain_.heightfield().min_height() - kFallGuardMargin;
    const RigidBody* car = phys_.body(vehicle_.body());
    const Heightfield& hf = terrain_.heightfield();
    const PlayerSlot* driving = slot(driver());
    const bool car_ours = role_ == SimRole::Client ? driver() == local_ : !(driving && driving->remote);
    if (has_car() && car_ours && car && ((car->pos.y < floor && default_gravity_at(car->pos)) || !body_state_valid(*car)
                || zone_out_of_bounds(hf, car->pos))) {
        reset_car();
    }
    for (PlayerSlot& s : slots_) {
        const Player& p = s.player;
        if (role_ == SimRole::Client && s.remote) {
            continue;
        }
        if (s.active && p.state() == PlayerState::OnFoot
            && ((p.pos().y < floor && default_gravity_at(p.pos())) || zone_out_of_bounds(hf, p.pos()))) {
            reset_player(s.id);
        }
    }
}

void Sim::rebuild_islands()
{
    const u32 craters_before = terrain_.crater_signature();
    islands_arena_.reset();
    islands_rebuild(islands_, world_, terrain_, phys_, islands_arena_, *scratch_);
    islands_signature_ = IslandField::signature(world_);
    islands_statics_ = phys_.statics().tri_count();
    islands_settle_ = 0.0f;
    if (terrain_.crater_signature() != craters_before) {
        terrain_dirty_ = true;
    }
    zone_gravity(world_, gravity_);
    islands_.fill_gravity(gravity_);
    jolt_.wakeAll();
}

void Sim::watch_islands(f32 dt)
{
    const bool statics_lost = phys_.statics().tri_count() != islands_statics_;
    if (IslandField::signature(world_) == islands_signature_ && !statics_lost) {
        islands_settle_ = 0.0f;
        return;
    }
    islands_settle_ += dt;
    if (statics_lost || islands_settle_ >= kIslandsSettle) {
        rebuild_islands();
    }
}

bool Sim::terminal_transform(Vec3& out_pos, Quat& out_rot) const
{
    if (carsys_.parts[PART_COMPUTER].installed) {
        const RigidBody* body = phys_.body(vehicle_.body());
        if (!body) {
            return false;
        }
        const Vec3 socket = part_def(PART_COMPUTER).socket_pos - vehicle_.config().com_offset;
        out_pos = body->pos + rotate(body->rot, socket);
        out_rot = body->rot * part_computer_rest_rot();
        return true;
    }
    if (const PlayerSlot* holder = holding(ITEM_COMPUTER)) {
        const Quat frame = holder->player.car_frame();
        f32 yaw = 0.0f;
        f32 pitch = 0.0f;
        frame_view_angles(frame, holder->view_dir, yaw, pitch);
        out_pos = hands_pos(holder->id);
        out_rot = frame * quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -yaw);
        return true;
    }
    if (const Entity* loose = find_pickup(ITEM_COMPUTER)) {
        out_pos = loose->pos;
        out_rot = loose->rot;
        return true;
    }
    return false;
}

void Sim::update_cables(f32 dt)
{
    const RigidBody* body = phys_.body(vehicle_.body());
    Vec3 term_pos;
    Quat term_rot = quat_identity();

    if (!body || !terminal_transform(term_pos, term_rot)) {
        for (u32 k = 0; k < CABLE_KIND_COUNT; k++) {
            if (carsys_.cables[k].state != CableState::Stowed) {
                carsys_.cables[k].reset();
            }
        }
        carsys_.coax_target = kCoaxTargetAntenna;
        carsys_.bus_target = kBusTargetCar;
        for (PlayerSlot& s : slots_) {
            s.interact.set_cable_drag(-1);
        }
        return;
    }

    const Vec3 car_origin = body->pos + rotate(body->rot, -vehicle_.config().com_offset);
    const Vec3 roots[2] = {kConnectorCoaxLocal, kConnectorBusLocal};
    const Vec3 jacks[2] = {car_layout().antenna_jack, car_layout().bay_jack};

    CableObstacle term_obstacle;
    term_obstacle.pos = term_pos;
    term_obstacle.rot = term_rot;
    term_obstacle.center = Vec3{0.0f, 0.0f, 0.03f};
    term_obstacle.half = Vec3{0.23f, 0.20f, 0.24f};

    Vec3 reel_anchor;
    bool have_reel = false;
    if (const PlayerSlot* holder = holding(ITEM_REEL)) {
        reel_anchor = hands_pos(holder->id);
        have_reel = true;
    } else if (const Entity* reel = find_pickup(ITEM_REEL)) {
        reel_anchor = reel->pos + Vec3{0.0f, 0.10f, 0.0f};
        have_reel = true;
    }

    for (u32 k = 0; k < CABLE_KIND_COUNT; k++) {
        Cable& cable = carsys_.cables[k];
        if (cable.state == CableState::Stowed) {
            cable.sim_init = false;
            continue;
        }
        if (cable.via_reel && !have_reel) {
            drop_cable(k);
            continue;
        }

        const Vec3* anchor = cable.via_reel ? &reel_anchor : nullptr;
        const Vec3 root = term_pos + rotate(term_rot, roots[k]);

        Vec3 end;
        if (cable.state == CableState::Plugged) {
            if (k == CABLE_COAX && carsys_.coax_target == kCoaxTargetCamera) {
                if (const PlayerSlot* holder = holding(ITEM_CAMERA)) {
                    end = hands_pos(holder->id);
                } else if (const Entity* cam = find_pickup(ITEM_CAMERA)) {
                    end = cam->pos + rotate(cam->rot, Vec3{-0.13f, 0.0f, 0.0f});
                } else {
                    cable.reset();
                    carsys_.coax_target = kCoaxTargetAntenna;
                    continue;
                }
            } else if (k == CABLE_BUS && carsys_.bus_target == kBusTargetTower) {
                end = tower_port_pos();
            } else if (k == CABLE_BUS && carsys_.bus_target == kBusTargetLoosePrinter) {
                const Entity* printer = find_pickup(ITEM_PRINTER, carsys_.bus_printer);
                if (!printer) {
                    drop_cable(k);
                    continue;
                }
                end = printer->pos + rotate(printer->rot, kLoosePrinterJackLocal);
            } else if (k == CABLE_BUS && carsys_.bus_target == kBusTargetPrinter) {
                if (!carsys_.parts[PART_PRINTER].installed) {
                    drop_cable(k);
                    continue;
                }
                end = car_origin + rotate(body->rot, car_layout().printer_jack);
            } else {
                end = car_origin + rotate(body->rot, jacks[k]);
            }
        } else {
            const PlayerSlot* dragger = nullptr;
            for (const PlayerSlot& s : slots_) {
                if (s.active && s.interact.cable_drag() == static_cast<i32>(k)) {
                    dragger = &s;
                }
            }
            if (!dragger) {
                drop_cable(k);
                continue;
            }
            end = dragger->view_origin + dragger->view_dir * 0.50f + Vec3{0.0f, -0.18f, 0.0f};
        }

        const f32 span = cable.span(root, end, anchor);
        const bool over_span = span > cable.max_len() * 1.06f;
        const bool over_arc = cable.sim_init && cable.current_length() > cable.max_len() * 1.18f;
        if (over_span || over_arc) {
            drop_cable(k);
            continue;
        }

        CableSimInput in;
        in.root = root;
        in.end = &end;
        in.anchor = anchor;
        in.terrain = &terrain_;
        in.phys = &phys_;
        in.car_pos = car_origin;
        in.car_rot = body->rot;
        std::array<CableObstacle, 1 + kCableNearPickups> obstacles;
        obstacles[0] = term_obstacle;
        u32 obstacle_count = 1;
        Vec3 lo = vec_min(root, end);
        Vec3 hi = vec_max(root, end);
        if (anchor) {
            lo = vec_min(lo, *anchor);
            hi = vec_max(hi, *anchor);
        }
        lo -= Vec3{2.0f, 2.0f, 2.0f};
        hi += Vec3{2.0f, 2.0f, 2.0f};
        for (u32 idx : world_.entities().live_indices()) {
            const Entity* e = world_.entities().at(idx);
            PickupState pickup;
            if (obstacle_count >= obstacles.size() || !e || e->kind != EntityKind::PartPickup
                || !pickup_body_state(phys_, *e, pickup)) {
                continue;
            }
            const Vec3 at = pickup.pos;
            if (at.x < lo.x || at.x > hi.x || at.y < lo.y || at.y > hi.y || at.z < lo.z || at.z > hi.z) {
                continue;
            }
            CableObstacle& o = obstacles[obstacle_count++];
            o.pos = at;
            o.rot = pickup.rot;
            o.center = Vec3{};
            o.half = item_cargo_half(static_cast<ItemKind>(e->aux_kind));
        }
        in.obstacles = obstacles.data();
        in.obstacle_count = obstacle_count;
        cable.sim(in, dt);
    }
}

void Sim::drop_cable(u32 kind)
{
    carsys_.cables[kind].reset();
    if (kind == CABLE_COAX) {
        carsys_.coax_target = kCoaxTargetAntenna;
    }
    if (kind == CABLE_BUS) {
        carsys_.bus_target = kBusTargetCar;
        carsys_.bus_printer = 0;
    }
    for (PlayerSlot& s : slots_) {
        if (s.interact.cable_drag() == static_cast<i32>(kind)) {
            s.interact.set_cable_drag(-1);
        }
    }
    if (const RigidBody* body = phys_.body(vehicle_.body())) {
        emit(ghost::game::CableDropped{to_glm(body_point(*body, Vec3{0.3f, 0.3f, 0.0f}))});
    }
}

void Sim::build_term_view(const PlayerCommand& cmd, TermView& out) const
{
    const RigidBody* body = phys_.body(vehicle_.body());

    out.sys = &carsys_;
    out.ammo = &gameplay_.ammo();
    out.veh = &vehicle_;
    out.phys = const_cast<PhysWorld*>(&phys_);
    out.terrain = &terrain_;
    out.time_of_day = time_of_day_;
    out.weather_rain = weather_.rain();
    out.weather_wetness = weather_.wetness();
    out.weather_mode = static_cast<i32>(weather_.mode());
    out.car_pos = body ? body->pos : Vec3{};
    out.garage_pos = spawn_.car_pos;
    out.speed_kmh = f_abs(vehicle_.forward_speed(phys_)) * 3.6f;
    out.rpm = drivetrain_rpm(vehicle_.train());
    out.orbit = terminal_user_ != kNoPlayer ? cmd.terminal_orbit : 0.0f;
    out.zoom = terminal_user_ != kNoPlayer ? cmd.terminal_zoom : 0.0f;

    const Cable& coax = carsys_.cables[CABLE_COAX];
    const Cable& bus = carsys_.cables[CABLE_BUS];
    out.travel_charge = travel_charge_;
    out.travel_primed = travel_primed_ >= 0;
    out.travel_ready = carsys_.parts[PART_COIL].installed;

    out.coax_state = coax.state == CableState::Plugged ? (coax.linked ? PORT_LINKED : PORT_PLUGGED) : PORT_UNPLUGGED;
    out.bus_state = bus.state == CableState::Plugged ? (bus.linked ? PORT_LINKED : PORT_PLUGGED) : PORT_UNPLUGGED;
    out.coax_camera = carsys_.coax_target == kCoaxTargetCamera;
    out.antenna_tier = !out.coax_camera && carsys_.parts[PART_ANTENNA].installed ? carsys_.parts[PART_ANTENNA].variant
                                                                                 : -1;
    out.bus_tower = carsys_.bus_target == kBusTargetTower;
    out.bus_printer = carsys_.bus_target == kBusTargetPrinter || carsys_.bus_target == kBusTargetLoosePrinter;
    out.synth_bay = carsys_.bus_bay(out.synth_tank, out.synth_printer);
    out.tower_breached = tower_breached_;
    out.tower_pos = tower_pos_;
}

void Sim::update_terminal(f32 dt)
{
    const PlayerCommand& cmd = commands_[terminal_user_ != kNoPlayer ? terminal_user_ : 0];
    if (carsys_.computer_on != term_prev_power_) {
        if (carsys_.computer_on) {
            terminal_.power(true);
            term_power_elapsed_ = 0.0f;
        } else {
            term_power_elapsed_ = f_min(term_power_elapsed_, 0.45f);
        }
        emit(ghost::game::TerminalPowered{carsys_.computer_on});
        term_prev_power_ = carsys_.computer_on;
    }
    if (carsys_.computer_on) {
        term_power_elapsed_ += dt;
    } else {
        term_power_elapsed_ = f_max(term_power_elapsed_ - dt * 1.6f, 0.0f);
        if (term_power_elapsed_ <= 0.0f && terminal_.powered()) {
            terminal_.power(false);
        }
    }

    if (terminal_user_ != kNoPlayer && !carsys_.computer_on) {
        terminal_user_ = kNoPlayer;
    }
    if (const RigidBody* body = phys_.body(vehicle_.body())) {
        terminal_.mapdata().visit(body->pos);
    }
    if (!carsys_.computer_on) {
        return;
    }

    TermView view;
    build_term_view(cmd, view);
    terminal_.set_disk(carsys_.floppy_disk);
    terminal_.update(view, dt);

    const TermRequest req = terminal_.take_request();
    if (req.travel_arm) {
        arm_travel(req.travel_destination);
    }
    if (req.travel_disarm) {
        travel_primed_ = -1;
    }
    if (req.breach_open) {
        tower_breached_ = true;
        emit(ghost::game::TowerBreached{});
    }
    if (req.tape_write && carsys_.tape_inserted >= 0) {
        carsys_.tape_inserted = 1 + req.tape_write_value;
        carsys_.deck_play = false;
        emit(ghost::game::TapeWritten{});
    }
    if (req.time_set) {
        time_of_day_ = req.time_value - std::floor(req.time_value);
    }
    if (req.weather_mode >= 0) {
        weather_.set_mode(static_cast<WeatherMode>(req.weather_mode));
    }
    for (u32 k = 0; k < 2; k++) {
        if (req.link_port[k] && carsys_.cables[k].state == CableState::Plugged) {
            carsys_.cables[k].linked = true;
            emit(ghost::game::PortLinked{static_cast<int>(k)});
        }
    }
    if (req.synth_print) {
        queue_print(req.synth_doses, req.synth_dose_count, req.synth_count);
    }
    if (req.power_off) {
        carsys_.computer_on = false;
        terminal_user_ = kNoPlayer;
    }
    if (terminal_.screen().take_click()) {
        emit(ghost::game::TerminalClicked{});
    }
}

void Sim::arm_travel(i32 destination)
{
    const std::span<const Destination> all = destinations();
    if (destination < 0 || static_cast<u32>(destination) >= all.size() || !all[static_cast<u32>(destination)].surveyed()
        || !carsys_.parts[PART_COIL].installed) {
        return;
    }
    travel_primed_ = destination;
    emit(ghost::game::TravelArmed{destination});
}

void Sim::begin_travel(i32 destination)
{
    const std::span<const Destination> all = destinations();
    if (destination < 0 || static_cast<u32>(destination) >= all.size() || !all[static_cast<u32>(destination)].surveyed()) {
        return;
    }
    travel_primed_ = -1;
    travel_charge_ = 0.0f;
    travel_charge_hold_ = false;
    travel_target_ = destination;
    travel_jump_ = 0.0f;
    travel_arrived_ = false;
    travel_carry_speed_ = 0.0f;
    if (const RigidBody* body = phys_.body(vehicle_.body())) {
        travel_carry_speed_ = length(Vec3{body->vel.x, 0.0f, body->vel.z});
    }
    emit(ghost::game::TravelStarted{destination});
}

void Sim::update_travel(f32 dt)
{
    if (!travel_charge_hold_) {
        const f32 speed_kmh = f_abs(vehicle_.forward_speed(phys_)) * 3.6f;
        if (carsys_.parts[PART_COIL].installed && speed_kmh >= kSpoolSpeedKmh) {
            travel_charge_ += dt / kSpoolSeconds;
        } else {
            travel_charge_ -= dt * kSpoolBleedRate;
        }
        travel_charge_ = f_clamp01(travel_charge_);
    }
    if (travel_primed_ >= 0 && !carsys_.parts[PART_COIL].installed) {
        travel_primed_ = -1;
    }
    if (travel_primed_ >= 0 && travel_charge_ >= 0.999f && travel_jump_ < 0.0f) {
        begin_travel(travel_primed_);
    }

    if (travel_jump_ < 0.0f) {
        return;
    }
    travel_jump_ += dt;
    if (!travel_arrived_ && travel_jump_ >= kJumpCollapse) {
        arrive_at_destination();
    }
    if (travel_jump_ >= kJumpCollapse + kJumpArrive) {
        travel_jump_ = -1.0f;
        travel_target_ = -1;
    }
}

bool Sim::load_zone(std::string_view dir)
{
    for (u32 idx : world_.entities().live_indices()) {
        Entity* e = world_.entities().at(idx);
        if (e && e->body != kNoEntityBody) {
            pickup_body_destroy(phys_, *e);
        }
    }
    world_.clear();
    phys_.statics_clear();
    zone_arena_.reset();
    mirrored_.clear();
    zone_serial_++;
    specimen_.id = 0;
    gameplay_.clearWorld();

    if (!zone_load(dir, zone_arena_, *scratch_, world_, phys_, terrain_, spawn_, &pickups_)) {
        log_error("travel: zone load failed for %.*s", static_cast<int>(dir.size()), dir.data());
        return false;
    }
    zone_gravity(world_, gravity_);
    rebuild_islands();
    car_before_valid_ = false;
    spawn_zone_ghosts();
    if (role_ != SimRole::Client) {
        release_lost_stores();
    }
    zone_dir_.assign(dir);
    apply_scene();

    const f32 tx = spawn_.tower_present ? spawn_.tower_x : 0.0f;
    const f32 tz = spawn_.tower_present ? spawn_.tower_z : 0.0f;
    tower_pos_ = Vec3{tx, terrain_.heightfield().sample(tx, tz), tz};
    tower_rot_ = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, spawn_.tower_yaw_deg * kDegToRad);
    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        if (e && (e->flags & kEntityFlagTower)) {
            tower_pos_ = e->pos;
            tower_rot_ = e->rot;
        }
    }

    interact_spawn_zone_pickups(world_, phys_, terrain_, pickups_);
    stock_fresh_tanks();
    terminal_.mapdata().init(terrain_.heightfield());
    terrain_dirty_ = true;
    tower_breached_ = false;
    emit(ghost::game::ZoneLoaded{scene_index_of_dir(dir)});
    log_info("travel: arrived in %.*s (zone arena %.1f MB)", static_cast<int>(dir.size()), dir.data(),
             static_cast<double>(zone_arena_.used()) / (1024.0 * 1024.0));
    return true;
}

void Sim::arrive_at_destination()
{
    const std::span<const Destination> all = destinations();
    if (travel_target_ < 0 || static_cast<u32>(travel_target_) >= all.size()) {
        return;
    }
    if (!load_zone(all[static_cast<u32>(travel_target_)].zone_dir)) {
        travel_target_ = -1;
        travel_jump_ = -1.0f;
        return;
    }

    RigidBody* body = phys_.body(vehicle_.body());
    if (body) {
        const f32 ground = terrain_.heightfield().sample(spawn_.car_pos.x, spawn_.car_pos.z);
        const Quat rot = quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, spawn_.car_yaw);
        const Vec3 pos{spawn_.car_pos.x, ground + vehicle_.config().half_extents.y + 0.35f, spawn_.car_pos.z};
        const Vec3 fwd = rotate(rot, Vec3{0.0f, 0.0f, -1.0f});

        body->pos = pos;
        body->prev_pos = pos;
        body->rot = rot;
        body->prev_rot = rot;
        body->vel = fwd * (travel_carry_speed_ * kJumpCarrySpeed);
        body->angular_vel = Vec3{};
        body->force_accum = Vec3{};
        body->torque_accum = Vec3{};
        body->asleep = 0;
        vehicle_.reset_contacts();
    }

    for (PlayerSlot& s : slots_) {
        if (s.active) {
            const Vec3 offset = spawn_point(s.id) - spawn_.player_pos;
            if (s.player.state() == PlayerState::OnFoot) {
                place_player(s, spawn_.car_pos + offset, spawn_.car_yaw);
            } else {
                s.player.teleport(spawn_.car_pos + offset, spawn_.car_yaw);
            }
        }
    }
    travel_arrived_ = true;
    emit(ghost::game::TravelArrived{travel_target_});
}

u64 Sim::checksum() const
{
    u64 h = 1469598103934665603ull;
    for (u32 idx : phys_.bodies().live_indices()) {
        const RigidBody* body = phys_.bodies().at(idx);
        if (body) {
            hash_bytes(h, &body->pos, sizeof(body->pos));
            hash_bytes(h, &body->rot, sizeof(body->rot));
            hash_bytes(h, &body->vel, sizeof(body->vel));
        }
    }
    for (u32 idx : world_.entities().live_indices()) {
        const Entity* e = world_.entities().at(idx);
        PickupState pickup;
        if (e && pickup_body_state(phys_, *e, pickup)) {
            hash_bytes(h, &pickup.pos, sizeof(pickup.pos));
            hash_bytes(h, &pickup.rot, sizeof(pickup.rot));
            hash_bytes(h, &pickup.vel, sizeof(pickup.vel));
        }
    }
    for (const PlayerSlot& s : slots_) {
        if (!s.active) {
            continue;
        }
        const Vec3 player_pos = s.player.pos();
        hash_bytes(h, &player_pos, sizeof(player_pos));
        const PlayerState state = s.player.state();
        hash_bytes(h, &state, sizeof(state));
        const Stance stance = s.player.stance();
        hash_bytes(h, &stance, sizeof(stance));
        const ItemKind held = s.interact.hands().kind;
        hash_bytes(h, &held, sizeof(held));
    }
    hash_bytes(h, &carsys_.fluids, sizeof(carsys_.fluids));
    hash_bytes(h, &carsys_.engine_on, sizeof(carsys_.engine_on));
    hash_bytes(h, &travel_charge_, sizeof(travel_charge_));
    const u32 entities = world_.count();
    hash_bytes(h, &entities, sizeof(entities));
    const u64 play = gameplay_.checksum();
    hash_bytes(h, &play, sizeof(play));
    return h;
}

}
