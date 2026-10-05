#include "sim/sim.h"
#include "world/pickup_body.h"
#include "core/log.h"
#include "game/ballistics/surface.h"
#include "engine/assets/asset_path.h"
#include "math/glm_bridge.h"
#include "physics/heightfield.h"
#include "terminal/program.h"
#include "world/destination.h"

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
constexpr f32 kImpactJump = 0.25f;
constexpr Vec3 kHoodLocal{0.0f, 0.45f, -1.4f};
constexpr Vec3 kTrunkLocal{0.0f, 0.45f, 1.6f};
constexpr Vec3 kEngineLocal{0.0f, 0.10f, -1.55f};
constexpr f32 kSlotSpacing = 1.2f;
constexpr u32 kCableNearPickups = 12;
constexpr f32 kVehiclePushShare = 1.0f;
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

struct CarSnapshot {
    bool door_target[2];
    bool hood_target;
    bool trunk_target;
    bool engine_on;
    f32 impact_cooldown;
    bool installed[PART_COUNT];
};

static CarSnapshot snapshot_car(const CarSys& sys)
{
    CarSnapshot s{};
    s.door_target[0] = sys.door_target[0];
    s.door_target[1] = sys.door_target[1];
    s.hood_target = sys.hood_target;
    s.trunk_target = sys.trunk_target;
    s.engine_on = sys.engine_on;
    s.impact_cooldown = sys.impact_cooldown;
    for (u32 i = 0; i < PART_COUNT; i++) {
        s.installed[i] = sys.parts[i].installed;
    }
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
    jolt_.setGravityField([this](const glm::vec3& p) { return to_glm(gravity_.gravity_at(from_glm(p))); });

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

    if (!vehicle_.init(phys_, scratch, "assets/cars/excel.cfg", spawn_.car_pos, spawn_.car_yaw)) {
        return false;
    }

    rules_ = ghost::game::loadPlayerRules(ghost::engine::assetPath("data"));
    for (u32 i = 0; i < kMaxPlayers; i++) {
        slots_[i] = PlayerSlot{};
        slots_[i].id = static_cast<PlayerId>(i);
    }
    add_player("Player");
    carsys_.init();
    weather_.init(20260718ull);
    tapes_.init();
    boxes_.init(scratch, "assets/cars/excel_interact.cfg");
    interact_spawn_zone_pickups(world_, phys_, terrain_, pickups_);

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

void Sim::apply_weather_grip()
{
    if (weather_.wetness() <= 0.002f) {
        return;
    }
    const RigidBody* body = phys_.body(vehicle_.body());
    const f32 road = body ? terrain_.road_amount(body->pos.x, body->pos.z) : 0.0f;
    const f32 wet_mul = 1.0f - weather_.wetness() * f_lerp(0.40f, 0.24f, road);
    for (u32 i = 0; i < kWheelCount; i++) {
        vehicle_.effects().tire_grip_mul[i] *= wet_mul;
    }
}

void Sim::step_physics(f32 dt)
{
    RigidBody* car = phys_.body(vehicle_.body());
    if (car) {
        if (jolt_.vehicle() == ghost::engine::kNoBody) {
            jolt_.setVehicle(to_glm(car->prev_pos), to_glm(car->prev_rot), to_glm(car->half_extents),
                             to_glm(car->box_offset), to_glm(car->vel), to_glm(car->angular_vel),
                             static_cast<std::uint64_t>(ghost::game::Surface::Steel));
        }
        jolt_.moveVehicle(to_glm(car->pos), to_glm(car->rot), dt);
    }
    jolt_.step(dt);
    const std::vector<ghost::engine::VehiclePush> pushes = jolt_.takeVehiclePushes();
    if (car && role_ != SimRole::Client) {
        for (const ghost::engine::VehiclePush& push : pushes) {
            body_apply_impulse_at_point(*car, from_glm(push.impulse) * kVehiclePushShare, from_glm(push.point));
        }
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
    return id < kMaxPlayers && slots_[id].active ? &slots_[id] : nullptr;
}

const PlayerSlot* Sim::slot(PlayerId id) const
{
    return id < kMaxPlayers && slots_[id].active ? &slots_[id] : nullptr;
}

PlayerId Sim::driver() const
{
    if (role_ == SimRole::Client) {
        const PlayerSlot& mine = slots_[local_];
        if (mine.active && mine.player.state() != PlayerState::OnFoot) {
            return local_;
        }
        return mirror_driver_ == local_ ? kNoPlayer : mirror_driver_;
    }
    if (const PlayerSlot* owner = slot(seat_owner_)) {
        if (owner->player.state() != PlayerState::OnFoot) {
            return seat_owner_;
        }
    }
    for (const PlayerSlot& s : slots_) {
        if (s.active && s.player.state() != PlayerState::OnFoot) {
            return s.id;
        }
    }
    return kNoPlayer;
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
    const Vec3 right = rotate(quat_from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, -spawn_.player_yaw), Vec3{1.0f, 0.0f, 0.0f});
    const f32 offset = static_cast<f32>(id) * kSlotSpacing;
    return spawn_.player_pos + right * offset;
}

void Sim::place_player(PlayerSlot& s, Vec3 feet, f32 yaw)
{
    s.player.set_character(s.id);
    s.player.init(feet, yaw);
    if (role_ == SimRole::Host && s.remote) {
        emit(ghost::game::PlayerPlaced{s.id, to_glm(feet), yaw});
    }
}

PlayerId Sim::add_player(std::string_view name, bool remote)
{
    for (PlayerSlot& s : slots_) {
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
        place_player(s, spawn_point(id), spawn_.player_yaw);
        commands_[id] = PlayerCommand{};
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
    jolt_.characterDestroy(id);
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
    place_player(*s, spawn_point(id), spawn_.player_yaw);
}

Vec3 Sim::hands_eye(PlayerId id) const
{
    const Player& p = slots_[id].player;
    return p.pos() + p.up() * kHandsEyeHeight;
}

Vec3 Sim::hands_pos(PlayerId id) const
{
    const PlayerSlot& s = slots_[id];
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

void Sim::gather_commands(std::span<const SlotCommand> commands, f32 dt)
{
    const bool client = role_ == SimRole::Client;
    for (PlayerSlot& s : slots_) {
        if (!s.active || (client && s.remote)) {
            continue;
        }
        PlayerCommand& cmd = commands_[s.id];
        if (s.dummy && !s.remote) {
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
    const PlayerSlot* s = slot(id);
    if (!s || s->player.state() != PlayerState::Driving) {
        VehicleInput parked;
        parked.handbrake = carsys_.handbrake_latched;
        vehicle_.set_input(parked);
        return;
    }
    const PlayerCommand& cmd = commands_[id];
    if (!cmd.gameplay || terminal_user_ == id) {
        vehicle_.set_input(VehicleInput{});
        return;
    }
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

void Sim::interact_slot(PlayerSlot& s, const PlayerCommand& cmd, PlayerCommand& player_cmd, f32 dt)
{
    player_cmd = cmd;
    player_cmd.interact = false;
    const bool busy = !cmd.gameplay || terminal_user_ == s.id;
    if (!busy) {
        const PlayerId seated = driver();
        Interact& interact = s.interact;
        interact.set_tower(true, tower_port_pos());
        InteractContext ctx;
        ctx.player = &s.player;
        ctx.veh = &vehicle_;
        ctx.sys = &carsys_;
        ctx.world = &world_;
        ctx.phys = &phys_;
        ctx.tapes = &tapes_;
        ctx.view_ray = Ray{s.view_origin, s.view_dir};
        ctx.e_down = cmd.use_down;
        ctx.e_pressed = cmd.use_pressed;
        ctx.seat_taken = seated != kNoPlayer && seated != s.id;
        ctx.preview = role_ == SimRole::Client;
        interact.update(boxes_, ctx, dt);
        if (interact.take_terminal_request() && carsys_.computer_on && terminal_user_ == kNoPlayer) {
            terminal_user_ = s.id;
        }
        s.player.set_speed_mul(f_max(1.0f - item_mass(interact.hands().kind) * 0.012f, 0.6f));
        if (cmd.use_pressed) {
            if (interact.action() == InteractAction::EnterCar && !ctx.seat_taken) {
                player_cmd.interact = true;
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
    std::array<ghost::game::RosterInput, kMaxPlayers> inputs{};
    u32 count = 0;
    for (const PlayerSlot& s : slots_) {
        if (s.active) {
            inputs[count++] = ghost::game::RosterInput{s.id, to_glm(s.player.pos()), s.use_down};
        }
    }
    const size_t first = events_.size();
    roster_.tick(dt, std::span<const ghost::game::RosterInput>(inputs.data(), count), events_);
    bool wiped = false;
    for (size_t i = first; i < events_.size(); i++) {
        wiped = wiped || std::holds_alternative<ghost::game::PlayerDied>(events_[i]);
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

    const CarSnapshot before = snapshot_car(carsys_);
    if (!client) {
        for (const SlotCommand& in : commands) {
            if (slot(in.id)) {
                apply_debug(in.cmd);
            }
        }
        adopt_remote_bodies(commands);
    }
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
                apply_hands(s, commands_[s.id]);
            }
        }
        if (terminal_user_ != kNoPlayer) {
            apply_terminal_input(commands_[terminal_user_]);
        }
    }

    std::array<PlayerCommand, kMaxPlayers> player_cmds{};
    if (!client) {
        carsys_.crank_request = false;
    }
    for (PlayerSlot& s : slots_) {
        if (!s.active || (client && s.remote)) {
            continue;
        }
        interact_slot(s, commands_[s.id], player_cmds[s.id], dt);
        if (commands_[s.id].crank && !client) {
            carsys_.crank_request = true;
        }
    }

    zone_gravity(world_, gravity_);
    islands_.fill_gravity(gravity_);
    weather_.tick(dt);
    carsys_.rain_level = weather_.rain();
    carsys_.tick(vehicle_, phys_, dt);
    apply_weather_grip();

    vehicle_.tick(phys_, dt);
    phys_.tick(dt);
    step_physics(dt);
    for (PlayerSlot& s : slots_) {
        if (!s.active || s.remote) {
            continue;
        }
        const PlayerId seated = driver();
        if (player_cmds[s.id].interact && s.player.state() == PlayerState::OnFoot && seated != kNoPlayer) {
            player_cmds[s.id].interact = false;
        }
        s.player.tick(phys_, &vehicle_, player_cmds[s.id], dt);
    }

    sync_pickup_transforms();
    update_cables(dt);
    if (client) {
        advance_client(dt);
        guard_against_falling();
        watch_islands(dt);
        events_.resize(first_event);
        events_.insert(events_.end(), from_host.begin(), from_host.end());
        tick_count_++;
        return;
    }
    arbitrate_seat();
    tick_roster(dt);
    commit_photo();
    update_terminal(dt);
    update_travel(dt);
    guard_against_falling();
    watch_islands(dt);
    emit_car_events(before);
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
        emit(ghost::game::HoodMoved{carsys_.hood_target, to_glm(body_point(*body, kHoodLocal - com))});
    }
    if (carsys_.trunk_target != before.trunk_target) {
        emit(ghost::game::TrunkMoved{carsys_.trunk_target, to_glm(body_point(*body, kTrunkLocal - com))});
    }
    if (carsys_.engine_on && !before.engine_on) {
        emit(ghost::game::EngineStarted{to_glm(body_point(*body, kEngineLocal - com))});
    }
    if (carsys_.impact_cooldown > before.impact_cooldown + kImpactJump) {
        emit(ghost::game::CarImpact{carsys_.impact_cooldown, to_glm(body->pos)});
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
    if (car_ours && car && ((car->pos.y < floor && default_gravity_at(car->pos)) || !body_state_valid(*car)
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
    const Vec3 jacks[2] = {kAntennaJackLocal, kBayJackLocal};

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

    if (!zone_load(dir, zone_arena_, *scratch_, world_, phys_, terrain_, spawn_, &pickups_)) {
        log_error("travel: zone load failed for %.*s", static_cast<int>(dir.size()), dir.data());
        return false;
    }
    zone_gravity(world_, gravity_);
    rebuild_islands();
    zone_dir_.assign(dir);

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
    terminal_.mapdata().init(terrain_.heightfield());
    terrain_dirty_ = true;
    tower_breached_ = false;
    i32 destination = -1;
    const std::span<const Destination> all = destinations();
    for (u32 i = 0; i < all.size(); i++) {
        if (all[i].zone_dir == dir) {
            destination = static_cast<i32>(i);
        }
    }
    emit(ghost::game::ZoneLoaded{destination});
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
        body->sleep_timer = 0.0f;
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
    return h;
}

}
