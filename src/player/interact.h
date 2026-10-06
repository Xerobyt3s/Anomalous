#pragma once

#include "carsys/cables.h"
#include "carsys/items.h"
#include "carsys/parts.h"
#include "core/fixed_string.h"
#include "core/types.h"
#include "math/vmath.h"
#include "world/entity.h"

#include <string_view>

namespace anom {
class Arena;
class CarSys;
class PhysWorld;
class Player;
class TapeLibrary;
class Terrain;
class Vehicle;
class World;
struct ZonePickups;

inline constexpr f32 kInteractRange = 2.6f;
inline constexpr f32 kInteractHoldTime = 1.2f;

enum InteractBoxId : u32 {
    IBOX_DOOR = 0,
    IBOX_DOOR_PANEL,
    IBOX_HANDBRAKE,
    IBOX_WIPER,
    IBOX_IGNITION,
    IBOX_DECK,
    IBOX_DISK_SLOT,
    IBOX_HOOD_LATCH,
    IBOX_HOOD_RAISED,
    IBOX_TRUNK_LID,
    IBOX_TRUNK_EDGE,
    IBOX_FUEL,
    IBOX_ANTENNA_JACK,
    IBOX_BAY_JACK,
    IBOX_COUNT,
};

struct InteractBox {
    FixedString<24> name;
    Vec3 center;
    Vec3 half;
};

enum class InteractAction : u32 {
    None = 0,
    Info,
    OpenDoor,
    CloseDoor,
    EnterCar,
    ExitCar,
    Handbrake,
    InsertKey,
    Crank,
    EngineOff,
    FuelCap,
    Computer,
    TerminalUse,
    RemovePart,
    InstallPart,
    ToggleHood,
    ToggleTrunk,
    PlaceCargo,
    TakeCargo,
    Pickup,
    Refuel,
    OilFill,
    CableGrab,
    CablePlug,
    CablePlugCamera,
    CablePlugTower,
    CableRouteReel,
    CableUnplug,
    DiskInsert,
    DiskEject,
    TapeInsert,
    TapeEject,
    Wipers,
    TakeRounds,
    CablePlugPrinter,
};

class InteractBoxes {
public:
    void init(Arena& scratch, std::string_view path);
    void poll(Arena& scratch, f64 now);
    bool save() const;

    InteractBox& box(u32 id) { return boxes_[id]; }
    const InteractBox& box(u32 id) const { return boxes_[id]; }

private:
    void load(Arena& scratch);

    InteractBox boxes_[IBOX_COUNT];
    FixedString<256> path_;
    i64 mtime_ = 0;
    f64 next_poll_ = 0.0;
};

struct InteractContext {
    Player* player = nullptr;
    Vehicle* veh = nullptr;
    CarSys* sys = nullptr;
    World* world = nullptr;
    PhysWorld* phys = nullptr;
    const TapeLibrary* tapes = nullptr;
    Ray view_ray;
    bool e_down = false;
    bool e_pressed = false;
    u32 seats_taken = 0;
    bool preview = false;
    bool gun_drawn = false;
};

class Interact {
public:
    void init();
    void update(const InteractBoxes& boxes, const InteractContext& ctx, f32 dt);

    bool drop(World& world, PhysWorld& phys, Vec3 origin, Vec3 dir, f32 power);

    Item& hands() { return hands_; }
    const Item& hands() const { return hands_; }
    bool has_key() const { return has_key_; }
    void set_has_key(bool v) { has_key_ = v; }
    InteractAction action() const { return action_; }
    std::string_view prompt() const { return prompt_.view(); }
    f32 hold_progress() const { return hold_progress_; }
    bool action_is_hold() const { return action_is_hold_; }
    i32 target_side() const { return target_side_; }
    i32 cable_drag() const { return cable_drag_; }
    void set_cable_drag(i32 v) { cable_drag_ = v; }
    Vec3 target_center() const { return target_center_; }
    Vec3 target_half() const { return target_half_; }
    Vec3 place_pos() const { return place_pos_; }

    bool take_terminal_request();
    bool take_holster_request();
    bool take_rounds_request();
    void adopt_holdings(const Interact& other);
    void set_tower(bool present, Vec3 port);

private:
    struct Candidate;

    void perform(const InteractContext& ctx);
    void resolve_doors(Candidate& best, const InteractBoxes& boxes, const InteractContext& ctx,
                       Ray local, bool on_foot) const;
    void resolve_in_car(Candidate& best, const InteractBoxes& boxes,
                        const InteractContext& ctx) const;
    void resolve_car_targets(Candidate& best, const InteractBoxes& boxes,
                             const InteractContext& ctx) const;
    void resolve_pickups(Candidate& best, const InteractBoxes& boxes,
                         const InteractContext& ctx) const;
    void resolve_tower_port(Candidate& best, const InteractContext& ctx) const;
    void consider_cable_root(Candidate& best, CarSys& sys, CableKind kind, f32 t, Vec3 center,
                             Vec3 half) const;
    void consider_cable_jack(Candidate& best, CarSys& sys, CableKind kind, f32 t, Vec3 center,
                             Vec3 half, std::string_view target_name) const;
    void consider_disk_slot(Candidate& best, CarSys& sys, f32 t, Vec3 center, Vec3 half) const;
    void consider_deck_slot(Candidate& best, CarSys& sys, const TapeLibrary* tapes, f32 t,
                            Vec3 center, Vec3 half) const;

    Item hands_;
    bool has_key_ = false;
    InteractAction action_ = InteractAction::None;
    PartKind target_part_ = PART_COUNT;
    EntityHandle target_entity_;
    i32 target_side_ = 0;
    u32 target_cargo_ = 0;
    Vec3 target_center_;
    Vec3 target_half_;
    Vec3 place_pos_;
    f32 hold_time_ = 0.0f;
    f32 hold_progress_ = 0.0f;
    bool action_is_hold_ = false;
    bool crank_latch_ = false;
    bool press_latch_ = false;
    bool use_terminal_request_ = false;
    bool holster_request_ = false;
    bool rounds_request_ = false;
    i32 target_cable_ = -1;
    i32 cable_drag_ = -1;
    bool tower_present_ = false;
    Vec3 tower_port_;
    FixedString<96> prompt_;
};

EntityHandle interact_spawn_pickup(World& world, PhysWorld& phys, Item item, Vec3 pos, f32 yaw,
                                   Vec3 vel);
void interact_spawn_zone_pickups(World& world, PhysWorld& phys, const Terrain& terrain,
                                 const ZonePickups& pickups);

}
