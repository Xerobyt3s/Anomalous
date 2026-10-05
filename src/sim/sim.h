#pragma once

#include "audio/tapes.h"
#include "carsys/carsys.h"
#include "carsys/items.h"
#include "core/arena.h"
#include "core/fixed_string.h"
#include "core/types.h"
#include "game/events.h"
#include "game/player/roster.h"
#include "math/vmath.h"
#include "physics/gravity_field.h"
#include "engine/physics/physics_world.h"
#include "physics/world.h"
#include "sim/player_slot.h"
#include "sim/wire.h"
#include "player/interact.h"
#include "player/player.h"
#include "terminal/disks.h"
#include "terminal/terminal.h"
#include "vehicle/vehicle.h"
#include "world/entity.h"
#include "world/islands/island_field.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/zone.h"

#include <array>
#include <span>
#include <string_view>
#include <vector>

namespace anom {
inline constexpr f32 kFixedDt = 1.0f / 120.0f;

struct CarSnapshot;

class Sim {
public:
    bool init(Arena& perm, Arena& scratch, std::string_view zone_dir);
    void tick(std::span<const SlotCommand> commands, f32 dt);
    void tick(const PlayerCommand& cmd, f32 dt);

    PlayerId add_player(std::string_view name, bool remote = false);
    void remove_player(PlayerId id);
    void set_role(SimRole role) { role_ = role; }
    SimRole role() const { return role_; }
    void client_reset(PlayerId local, Vec3 feet, f32 yaw, std::string_view name);
    PlayerId become_solo(PlayerId local);
    void make_snapshot(WorldSnapshot& out) const;
    void queue_snapshot(WorldSnapshot&& snapshot);
    void queue_host_event(const ghost::game::GameEvent& event) { host_events_.push_back(event); }
    void queue_term_mirror(const TermMirror& mirror);
    bool switch_zone(std::string_view dir) { return load_zone(dir); }
    u32 zone_serial() const { return zone_serial_; }
    PlayerId local_id() const { return local_; }
    void set_local_id(PlayerId id) { local_ = id; }
    void reset_car();
    void reset_player(PlayerId id);
    void guard_against_falling();
    void watch_islands(f32 dt);
    void rebuild_islands();
    void poll_hot_reload(Arena& scratch, f64 now);
    void queue_photo(const u8* rgb, PlayerId by = 0);
    void arm_travel(i32 destination);
    void set_travel_charge(f32 charge) { travel_charge_ = charge; travel_charge_hold_ = true; }

    ghost::game::EventList& events() { return events_; }

    Terrain& terrain() { return terrain_; }
    const Terrain& terrain() const { return terrain_; }
    World& world() { return world_; }
    const World& world() const { return world_; }
    PhysWorld& phys() { return phys_; }
    ghost::engine::PhysicsWorld& physics() { return jolt_; }
    const ghost::engine::PhysicsWorld& physics() const { return jolt_; }
    const PhysWorld& phys() const { return phys_; }
    GravityField& gravity() { return gravity_; }
    Vehicle& vehicle() { return vehicle_; }
    const Vehicle& vehicle() const { return vehicle_; }
    PlayerSlot* slot(PlayerId id);
    const PlayerSlot* slot(PlayerId id) const;
    std::span<PlayerSlot> slots() { return slots_; }
    std::span<const PlayerSlot> slots() const { return slots_; }
    Player& player(PlayerId id) { return slots_[id].player; }
    const Player& player(PlayerId id) const { return slots_[id].player; }
    Interact& interact(PlayerId id) { return slots_[id].interact; }
    const Interact& interact(PlayerId id) const { return slots_[id].interact; }
    PlayerId driver() const;
    ghost::game::Roster& roster() { return roster_; }
    const ghost::game::Roster& roster() const { return roster_; }
    const ghost::game::PlayerRules& player_rules() const { return rules_; }
    CarSys& carsys() { return carsys_; }
    const CarSys& carsys() const { return carsys_; }
    InteractBoxes& boxes() { return boxes_; }
    Weather& weather() { return weather_; }
    const Weather& weather() const { return weather_; }
    TapeLibrary& tapes() { return tapes_; }
    DiskStore& disks() { return disks_; }
    const DiskStore& disks() const { return disks_; }
    Terminal& terminal() { return terminal_; }
    const Terminal& terminal() const { return terminal_; }
    IslandField& islands() { return islands_; }
    const IslandField& islands() const { return islands_; }
    const ZoneSpawn& spawn() const { return spawn_; }
    std::string_view zone_dir() const { return zone_dir_.view(); }

    f32 time_of_day() const { return time_of_day_; }
    void set_time_of_day(f32 t) { time_of_day_ = t; }
    f32 travel_charge() const { return travel_charge_; }
    f32 travel_jump() const { return travel_jump_; }
    PlayerId terminal_user() const { return terminal_user_; }
    void set_terminal_user(PlayerId id) { terminal_user_ = id; }
    f32 terminal_power_seconds() const { return term_power_elapsed_; }
    Vec3 tower_pos() const { return tower_pos_; }
    Quat tower_rot() const { return tower_rot_; }
    Vec3 tower_port_pos() const;
    bool tower_breached() const { return tower_breached_; }
    u64 tick_count() const { return tick_count_; }
    bool take_terrain_dirty();
    const Entity* find_pickup(ItemKind kind) const;
    Vec3 hands_pos(PlayerId id) const;
    Vec3 hands_eye(PlayerId id) const;
    const PlayerSlot* holding(ItemKind kind) const;
    u64 checksum() const;

private:
    void gather_commands(std::span<const SlotCommand> commands, f32 dt);
    void adopt_remote_bodies(std::span<const SlotCommand> commands);
    void sync_remote_character(PlayerSlot& slot);
    void arbitrate_seat();
    void apply_snapshot(const WorldSnapshot& snapshot);
    void adopt_pickups(const std::vector<PickupWire>& pickups);
    void apply_host_events();
    void advance_client(f32 dt);
    void apply_debug(const PlayerCommand& cmd);
    void apply_car_controls();
    void apply_hands(PlayerSlot& slot, const PlayerCommand& cmd);
    void apply_terminal_input(const PlayerCommand& cmd);
    void interact_slot(PlayerSlot& slot, const PlayerCommand& cmd, PlayerCommand& player_cmd, f32 dt);
    void tick_roster(f32 dt);
    PlayerCommand dummy_command(PlayerSlot& slot, f32 dt) const;
    const PlayerSlot* nearest_human(Vec3 from) const;
    Vec3 spawn_point(PlayerId id) const;
    PlayerId spawn_dummy();
    void place_player(PlayerSlot& slot, Vec3 feet, f32 yaw);
    void apply_weather_grip();
    void sync_pickup_transforms();
    void step_physics(f32 dt);
    bool default_gravity_at(Vec3 p) const;
    void update_cables(f32 dt);
    void drop_cable(u32 kind);
    bool terminal_transform(Vec3& out_pos, Quat& out_rot) const;
    void update_terminal(f32 dt);
    void build_term_view(const PlayerCommand& cmd, TermView& out) const;
    void update_travel(f32 dt);
    void begin_travel(i32 destination);
    void arrive_at_destination();
    bool load_zone(std::string_view dir);
    void commit_photo();
    void emit_car_events(const CarSnapshot& before);
    void emit(ghost::game::GameEvent event) { events_.push_back(event); }

    Terrain terrain_;
    World world_;
    ghost::engine::PhysicsWorld jolt_;
    GravityField gravity_;
    PhysWorld phys_;
    Vehicle vehicle_;
    std::array<PlayerSlot, kMaxPlayers> slots_{};
    std::array<PlayerCommand, kMaxPlayers> commands_{};
    ghost::game::PlayerRules rules_;
    ghost::game::Roster roster_{rules_};
    CarSys carsys_;
    InteractBoxes boxes_;
    Weather weather_;
    TapeLibrary tapes_;
    DiskStore disks_;
    Terminal terminal_{tapes_};

    ZoneSpawn spawn_;
    ZonePickups pickups_;
    FixedString<128> zone_dir_;
    Arena* perm_ = nullptr;
    Arena* scratch_ = nullptr;
    Arena zone_arena_;
    Arena islands_arena_;
    IslandField islands_;
    u64 islands_signature_ = 0;
    u32 islands_statics_ = 0;
    f32 islands_settle_ = 0.0f;

    ghost::game::EventList events_;
    ghost::game::EventList host_events_;
    std::vector<WorldSnapshot> snapshots_;
    std::vector<TermMirror> term_mirrors_;
    struct Mirrored {
        u32 idx = 0;
        u32 gen = 0;
        EntityHandle local;
    };
    std::vector<Mirrored> mirrored_;
    SimRole role_ = SimRole::Solo;
    PlayerId local_ = 0;
    PlayerId seat_owner_ = kNoPlayer;
    PlayerId mirror_driver_ = kNoPlayer;
    u32 host_zone_serial_ = 0;
    u32 zone_serial_ = 0;
    PlayerId photo_by_ = 0;

    f32 travel_charge_ = 0.0f;
    bool travel_charge_hold_ = false;
    i32 travel_target_ = -1;
    i32 travel_primed_ = -1;
    f32 travel_jump_ = -1.0f;
    bool travel_arrived_ = false;
    f32 travel_carry_speed_ = 0.0f;
    bool terrain_dirty_ = false;

    Vec3 tower_pos_{};
    Quat tower_rot_ = quat_identity();
    bool tower_breached_ = false;
    f32 time_of_day_ = 0.32f;

    PlayerId terminal_user_ = kNoPlayer;
    bool term_prev_power_ = false;
    f32 term_power_elapsed_ = 0.0f;

    std::vector<u8> photo_;
    bool photo_pending_ = false;
    u64 tick_count_ = 0;
};

}
