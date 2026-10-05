#pragma once

#include "core/fixed_string.h"
#include "core/types.h"
#include "game/player/player_hit.h"
#include "math/vmath.h"
#include "player/interact.h"
#include "player/player.h"
#include "vehicle/vehicle.h"

namespace anom {
using ghost::game::kMaxPlayers;
using ghost::game::kNoPlayer;
using ghost::game::PlayerId;

inline constexpr Vec3 kSlotColors[kMaxPlayers] = {
    {0.55f, 0.6f, 1.0f}, {1.0f, 0.62f, 0.2f}, {0.95f, 0.45f, 0.5f}, {0.45f, 0.8f, 0.55f}};

enum class DummyScript : u8 {
    Stand,
    Strafe,
    Revive,
    Run,
    Shroud,
    Crawl,
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
};

struct SlotCommand {
    PlayerId id = 0;
    PlayerCommand cmd;
    bool has_body = false;
    Player body;
    bool has_car = false;
    CarWire car;
};

enum class SimRole : u8 {
    Solo,
    Host,
    Client,
};

std::string_view dummy_script_name(DummyScript script);

}
