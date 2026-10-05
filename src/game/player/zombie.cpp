#include "game/player/zombie.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
float hash(float n) {
    const float s = std::sin(n * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

}

bool zombieRamming(const PlayerState& self, const ZombieTuning& tuning) {
    const bool thrown = !self.grounded || self.stance == Stance::Dive;
    return thrown && glm::length(self.velocity) >= tuning.ramSpeed;
}

ZombieOrder zombieThink(ZombieMind& mind, const ZombieTuning& tuning, const PlayerState& self, std::span<const ZombieTarget> targets,
                        int liveRounds, float dt) {
    mind.age += dt;
    mind.cooldown = std::max(0.0f, mind.cooldown - dt);
    ZombieOrder order;
    order.command.yaw = self.yaw;
    order.command.pitch = self.pitch;
    const glm::vec3 eye = self.position + glm::vec3(0.0f, self.eyeHeight, 0.0f);
    order.command.muzzle = eye;
    order.aim = glm::vec3(std::cos(self.pitch) * std::sin(self.yaw), std::sin(self.pitch), -std::cos(self.pitch) * std::cos(self.yaw));
    order.command.barrelDirection = order.aim;

    const ZombieTarget* target = nullptr;
    float best = 1e9f;
    for (const ZombieTarget& t : targets) {
        const float distance = glm::distance(t.feet, self.position) + (t.visible ? 0.0f : 30.0f);
        if (distance < best) {
            best = distance;
            target = &t;
        }
    }
    if (!target) {
        return order;
    }

    const bool shooting = liveRounds > 0;
    const float wander = tuning.aimWander * (shooting ? 1.0f : 0.25f);
    const glm::vec3 to = target->chest - eye;
    const float flat = std::max(glm::length(glm::vec2(to.x, to.z)), 1e-3f);
    order.command.yaw = std::atan2(to.x, -to.z) + wander * std::sin(mind.age * 1.7f + mind.seed);
    order.command.pitch = std::atan2(to.y, flat) + wander * 0.6f * std::sin(mind.age * 2.3f + mind.seed * 2.0f);
    order.aim = glm::vec3(std::cos(order.command.pitch) * std::sin(order.command.yaw), std::sin(order.command.pitch),
                          -std::cos(order.command.pitch) * std::cos(order.command.yaw));
    order.command.barrelDirection = order.aim;
    if (mind.age < tuning.riseTime) {
        mind.anchor = self.position;
        return order;
    }

    if (self.stance == Stance::Crawl && self.grounded) {
        mind.lying += dt;
        mind.anchorAge = 0.0f;
        if (mind.lying >= tuning.standDelay) {
            order.command.crawl = true;
            mind.lying = 0.0f;
        }
        return order;
    }
    mind.lying = 0.0f;

    constexpr float kHeadwayEvery = 0.7f;
    constexpr float kHeadway = 0.35f;
    constexpr float kSidestep = 0.9f;
    mind.anchorAge += dt;
    mind.sidestep = std::max(0.0f, mind.sidestep - dt);
    if (mind.anchorAge >= kHeadwayEvery) {
        if (glm::distance(self.position, mind.anchor) < kHeadway && mind.sidestep <= 0.0f) {
            mind.sidestep = kSidestep;
            mind.side = -mind.side;
            mind.leap = -1.0f;
        }
        mind.anchor = self.position;
        mind.anchorAge = 0.0f;
    }
    if (mind.sidestep > 0.0f && self.grounded) {
        order.command.move = glm::vec2(mind.side, 0.25f);
        order.command.sprint = !shooting;
        return order;
    }

    if (shooting) {
        order.command.move = glm::vec2(0.0f, target->visible ? 0.5f : 1.0f);
        mind.shotTimer -= dt;
        if (target->visible && mind.shotTimer <= 0.0f) {
            order.fire = true;
            ++mind.shots;
            mind.shotTimer = glm::mix(tuning.shotEveryMin, tuning.shotEveryMax, hash(static_cast<float>(mind.shots) + mind.seed));
        }
        return order;
    }

    if (mind.leap < 0.0f && self.grounded && flat < tuning.diveMin && target->visible) {
        order.command.move = glm::vec2(0.0f, -1.0f);
        return order;
    }
    order.command.move = glm::vec2(0.0f, 1.0f);
    order.command.sprint = true;
    if (mind.leap >= 0.0f) {
        mind.leap += dt;
        if (!self.grounded && !self.diveUsed && mind.leap >= tuning.diveDelay) {
            order.command.jump = true;
        }
        if (self.grounded && mind.leap > tuning.diveDelay + 0.1f) {
            mind.leap = -1.0f;
            mind.cooldown = tuning.leapCooldown;
        }
    } else if (self.grounded && self.stance == Stance::Stand && mind.cooldown <= 0.0f && flat < tuning.diveRange && flat > tuning.diveMin &&
               target->visible) {
        order.command.jump = true;
        mind.leap = 0.0f;
    }
    return order;
}

}
