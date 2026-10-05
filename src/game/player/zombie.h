#pragma once

#include "game/player/player.h"

#include <glm/glm.hpp>

#include <span>

namespace ghost::game {
struct ZombieTuning {
    float health = 1.0f;
    float riseTime = 1.2f;
    float shotEveryMin = 0.6f;
    float shotEveryMax = 0.9f;
    float spreadDeg = 6.0f;
    float aimWander = 0.1f;
    float diveRange = 5.0f;
    float diveMin = 1.2f;
    float diveDelay = 0.18f;
    float leapCooldown = 0.9f;
    float standDelay = 0.5f;
    float ramSpeed = 3.0f;
    float ramDamage = 0.2f;
    float ramShove = 5.0f;
    float ramCooldown = 1.0f;
};

struct ZombieTarget {
    glm::vec3 feet{0.0f};
    glm::vec3 chest{0.0f};
    bool visible = true;
};

struct ZombieMind {
    float age = 0.0f;
    float shotTimer = 0.5f;
    float leap = -1.0f;
    float cooldown = 0.0f;
    float lying = 0.0f;
    float seed = 0.0f;
    int shots = 0;
    glm::vec3 anchor{0.0f};
    float anchorAge = 0.0f;
    float sidestep = 0.0f;
    float side = 1.0f;
};

struct ZombieOrder {
    PlayerCommand command;
    bool fire = false;
    glm::vec3 aim{0.0f, 0.0f, -1.0f};
};

ZombieOrder zombieThink(ZombieMind& mind, const ZombieTuning& tuning, const PlayerState& self, std::span<const ZombieTarget> targets,
                        int liveRounds, float dt);

bool zombieRamming(const PlayerState& self, const ZombieTuning& tuning);

}
