#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "game/player/player_hit.h"
#include "math/vmath.h"

#include <utility>
#include <vector>
#include "player/interact.h"
#include "player/player.h"
#include "game/player/zombie.h"
#include "sim/gameplay.h"
#include "vehicle/vehicle.h"

namespace anom {
using ghost::game::kMaxPlayers;
using ghost::game::kZombieIdBase;

inline constexpr u32 kMaxSlots = kMaxPlayers * 2;

constexpr u32 slot_index(u8 id)
{
    if (id < kMaxPlayers) {
        return id;
    }
    if (id >= kZombieIdBase && id < kZombieIdBase + kMaxPlayers) {
        return kMaxPlayers + (id - kZombieIdBase);
    }
    return kMaxSlots;
}

constexpr u8 slot_id(u32 index)
{
    return static_cast<u8>(index < kMaxPlayers ? index : kZombieIdBase + (index - kMaxPlayers));
}
using ghost::game::kNoPlayer;
using ghost::game::PlayerId;

inline constexpr Vec3 kSlotColors[kMaxPlayers] = {
    {0.55f, 0.6f, 1.0f}, {1.0f, 0.62f, 0.2f}, {0.95f, 0.45f, 0.5f}, {0.45f, 0.8f, 0.55f}};

enum class DummyScript : u8 {
    Stand,
    Strafe,
    Shoot,
    Revive,
    Run,
    Shroud,
    Crawl,
    Reload,
    Count,
};

struct PlayerSlot {
    PlayerId id = kNoPlayer;
    bool active = false;
    bool dummy = false;
    bool remote = false;
    FixedString<32> name;
    Vec3 color{0.8f, 0.8f, 0.8f};
    Player player;
    Interact interact;
    Vec3 view_origin{};
    Vec3 view_dir{0.0f, 0.0f, -1.0f};
    bool use_down = false;
    DummyScript script = DummyScript::Stand;
    f32 script_time = 0.0f;
    f32 shot_timer = 2.0f;
    ghost::game::PlayerGun gun;
    PlayerId zombie_of = kNoPlayer;
    ghost::game::ZombieMind mind;
    std::vector<std::pair<PlayerId, f32>> ram_cooled;
    bool was_holstered = true;
    bool take_pending = false;
    f32 lowering = -1.0f;
    f32 bench_time = 0.0f;
    bool at_bench = false;
    bool window_blocked = false;
    u8 cowboy = 0;
};

struct SlotCommand {
    PlayerId id = 0;
    PlayerCommand cmd;
    bool has_body = false;
    Player body;
    bool has_car = false;
    CarWire car;
    bool has_gun = false;
    ghost::game::MechanismView gun;
};

enum class SimRole : u8 {
    Solo,
    Host,
    Client,
};

std::string_view dummy_script_name(DummyScript script);

}
