#pragma once

#include "game/ammo/ammo_data.h"
#include "game/player/zombie.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghost::game {
using GhostTypeId = std::uint16_t;

enum class GhostBehavior : std::uint8_t { Wisp, Poltergeist, BallLightning, Mimic, Vasskraka, Necromite };

struct WispParams {
    float hoverHeight = 1.5f;
    float wanderSpeed = 0.8f;
    float wanderRadius = 4.0f;
    float lureDistance = 7.0f;
    float lureSpeed = 3.8f;
    float sway = 0.6f;
    float swaySpeed = 0.6f;
    float rushSpeed = 11.0f;
    float rushTrigger = 2.5f;
    float lookAwayTime = 0.7f;
    float burstRadius = 1.6f;
    float burstDamage = 0.45f;
    float flinchTime = 0.6f;
};

struct PoltergeistParams {
    float hoverHeight = 1.6f;
    float speed = 2.5f;
    float wanderRadius = 2.5f;
    float grabRange = 7.0f;
    float liftHeight = 1.4f;
    float holdTime = 1.1f;
    float throwSpeed = 15.0f;
    float throwDamage = 0.25f;
    float cooldown = 2.2f;
    float flinchTime = 0.8f;
};

struct BallLightningParams {
    float hoverHeight = 1.6f;
    float chargeTime = 2.5f;
    float arcEvery = 0.55f;
    int arcsPerVolley = 2;
    float arcReach = 5.0f;
    float arcDelay = 0.35f;
    float arcRadius = 0.9f;
    float arcDamage = 0.12f;
    int hops = 3;
    float hopEvery = 0.18f;
    float hopMin = 3.0f;
    float hopMax = 5.0f;
    float circleRange = 6.0f;
    float circleRadius = 3.5f;
    float flinchTime = 0.35f;
    float wanderSpeed = 0.6f;
    float wanderRadius = 3.0f;
    float driftSpeed = 0.25f;
};

struct MimicParams {
    float bodyRadius = 0.25f;
    float standHeight = 0.4f;
    float disguisedRadius = 0.3f;
    float revealNear = 2.2f;
    float revealLinger = 2.5f;
    float revealGrab = 0.3f;
    float revealTime = 0.8f;
    float revealRadius = 1.2f;
    float revealDamage = 0.15f;
    float revealShove = 4.0f;
    float burstSpeed = 7.0f;
    float turnRate = 5.0f;
    float acceleration = 16.0f;
    float burstMin = 0.4f;
    float burstMax = 0.7f;
    float pauseMin = 0.6f;
    float pauseMax = 1.2f;
    float climbHeight = 2.5f;
    float dropRange = 1.5f;
    float wallSearch = 3.0f;
    float leapMin = 3.0f;
    float leapMax = 5.0f;
    float leapSpeed = 7.0f;
    float thrashRange = 1.6f;
    float thrashReach = 1.8f;
    float thrashWindup = 0.3f;
    float thrashDamage = 0.18f;
    float thrashShove = 4.0f;
    float thrashCooldown = 0.9f;
    float flinchTime = 0.3f;
    float leapDamage = 0.15f;
    float leapHitRadius = 0.6f;
    float leapShove = 3.5f;
    float spotMin = 2.0f;
    float spotMax = 7.0f;
    float patience = 5.0f;
    float senseRange = 10.0f;
    float fleeMin = 8.0f;
    float fleeMax = 14.0f;
    float concealTime = 0.6f;
    float boxChance = 0.33f;
    std::vector<MaterialId> disguises;
};

struct VasskrakaParams {
    float roostSearch = 12.0f;
    float returnSpeed = 8.0f;
    float flySpeed = 13.0f;
    float circleRadius = 8.0f;
    float circleHeight = 3.0f;
    float circleSpeed = 5.0f;
    float attackEvery = 3.5f;
    float ballChance = 0.15f;
    float diveWindup = 0.9f;
    float diveSpeed = 11.0f;
    float diveRadius = 0.7f;
    float diveDamage = 0.2f;
    float diveShove = 4.0f;
    float gatherTime = 1.3f;
    float ballHeight = 3.0f;
    float burstRadius = 3.0f;
    float burstDamage = 0.25f;
    float burstShove = 5.0f;
    float reformTime = 1.5f;
    float scatterTime = 0.9f;
    float scatterDistance = 2.5f;
    float mergeRange = 12.0f;
    float maxHealth = 1.5f;
    float splitAbove = 0.6f;
    float splitCooldown = 6.0f;
    float pushDamage = 2.0f;
    float minSize = 0.3f;
    float fallLean = 1.2f;
    float firstAttack = 2.0f;
};

struct NecromiteParams {
    float spawnChance = 1.0f;
    float spawnDelay = 3.0f;
    float spawnMin = 6.0f;
    float spawnMax = 10.0f;
    float emergeTime = 0.8f;
    float crawlSpeed = 2.5f;
    float enterRange = 0.35f;
    float enterTime = 0.6f;
    ZombieTuning zombie;
};

struct GhostDrop {
    MaterialId material = 0;
    int count = 1;
};

struct GhostDef {
    std::string name;
    std::string display;
    GhostBehavior behavior = GhostBehavior::Wisp;
    float health = 30.0f;
    float radius = 0.3f;
    float weight = 0.5f;
    float sightRange = 20.0f;
    bool seesThroughWalls = false;
    float hearingRange = 40.0f;
    float memory = 4.0f;
    float dodgeChance = 0.0f;
    std::array<float, static_cast<std::size_t>(DamageKind::Count)> damage{1.0f, 1.0f, 1.0f, 1.0f};
    std::vector<GhostDrop> drops;
    WispParams wisp;
    PoltergeistParams poltergeist;
    BallLightningParams ballLightning;
    MimicParams mimic;
    VasskrakaParams vasskraka;
    NecromiteParams necromite;
    float dropDisguiseChance = 0.0f;
    bool invisible = false;
    bool windImmune = false;
    float hitstopMin = 0.06f;
    float hitstopMax = 0.14f;
};

struct CaughtTuning {
    float catchSpeed = 2.5f;
    float releaseTime = 0.4f;
    float grip = 5.0f;
    float tumbleSpin = 6.0f;
    float fallShare = 0.25f;
};

struct GhostData {
    std::vector<GhostDef> types;
    float roundDamage = 25.0f;
    CaughtTuning caught;

    std::optional<GhostTypeId> find(std::string_view name) const;
    GhostTypeId type(std::string_view name) const;
};

constexpr std::int16_t kDisguiseBox = 1000;

std::int16_t pickDisguise(const GhostDef& def, float kind, float which);

std::optional<GhostDrop> chooseDrop(const GhostDef& def, std::int16_t disguise, float pick, float chance);

GhostData parseGhostData(std::string_view ghostsJson, const AmmoData& ammo);
GhostData loadGhostData(const std::filesystem::path& dataDirectory, const AmmoData& ammo);

}
