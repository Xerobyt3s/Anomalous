#pragma once

#include "carsys/carsys.h"
#include "carsys/items.h"
#include "core/types.h"
#include "game/player/roster.h"
#include "math/vmath.h"
#include "player/interact.h"
#include "player/player.h"
#include "sim/player_slot.h"
#include "vehicle/vehicle.h"
#include "world/weather.h"

#include <type_traits>
#include <vector>

namespace anom {
inline constexpr u32 kWireNameChars = 24;

struct SlotWire {
    PlayerId id = kNoPlayer;
    bool dummy = false;
    bool use_down = false;
    char name[kWireNameChars] = {};
    Vec3 color{0.8f, 0.8f, 0.8f};
    Player player;
    Interact interact;
};

struct PickupWire {
    u32 idx = 0;
    u32 gen = 0;
    Item item;
    Vec3 pos{};
    Quat rot = quat_identity();
    Vec3 vel{};
    Vec3 angular_vel{};
    bool has_body = false;
};

struct WorldHeader {
    u32 tick = 0;
    u32 zone_serial = 0;
    f32 time_of_day = 0.0f;
    Weather weather;
    CarSys carsys;
    CarWire car;
    PlayerId terminal_user = kNoPlayer;
    PlayerId driver = kNoPlayer;
    f32 travel_charge = 0.0f;
    i32 travel_primed = -1;
    f32 travel_jump = -1.0f;
    i32 travel_target = -1;
    bool tower_breached = false;
    Vec3 tower_pos{};
    Quat tower_rot = quat_identity();
};

struct WorldSnapshot {
    WorldHeader header;
    std::vector<SlotWire> players;
    std::vector<ghost::game::RosterEntry> roster;
    std::vector<PickupWire> pickups;
};

static_assert(std::is_trivially_copyable_v<SlotWire>);
static_assert(std::is_trivially_copyable_v<PickupWire>);
static_assert(std::is_trivially_copyable_v<WorldHeader>);
static_assert(std::is_trivially_copyable_v<PlayerCommand>);

}
