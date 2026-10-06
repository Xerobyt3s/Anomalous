#include "game/ghosts/vasskraka.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {

void enter(Ghost& ghost, GhostState state) {
    ghost.state = state;
    ghost.stateTime = 0.0f;
}

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-4f ? v / length : fallback;
}

glm::vec3 chestOf(const GhostQuarry& quarry) { return glm::mix(quarry.feet, quarry.eye, 0.6f); }

bool blocked(const GhostContext& context, const glm::vec3& from, const glm::vec3& to) {
    return context.blocked && context.blocked(from, to);
}

void fly(Ghost& ghost, const glm::vec3& target, float speed, float response, float dt) {
    const glm::vec3 to = target - ghost.position;
    const float distance = glm::length(to);
    const glm::vec3 wanted = distance > 1e-3f ? to / distance * std::min(speed, distance * 4.0f) : glm::vec3(0.0f);
    ghost.velocity += (wanted - ghost.velocity) * std::min(1.0f, response * dt);
}

void scatter(Ghost& ghost, const glm::vec3& to, GhostAccess& access) {
    ghost.goal = to;
    ghost.attached = false;
    enter(ghost, GhostState::Scatter);
    access.events.push_back(KrakaScattered{ghost.id, ghost.position});
}

float nextAttack(const VasskrakaParams& p, GhostAccess& access) { return p.attackEvery * (0.7f + 0.6f * access.random01()); }

std::optional<GhostSurfaceHit> wallToRoostOn(const Ghost& ghost, const VasskrakaParams& p, const GhostContext& context) {
    std::optional<GhostSurfaceHit> best;
    float bestDistance = 1e9f;
    for (int k = 0; k < 12; ++k) {
        const float angle = static_cast<float>(k) * glm::two_pi<float>() / 12.0f + ghost.seed;
        const glm::vec3 direction = glm::normalize(upBasis(ghost.up) * glm::vec3(std::cos(angle), 0.15f, std::sin(angle)));
        if (const auto hit = castSurface(context, ghost.position, ghost.position + direction * p.roostSearch)) {
            const float distance = glm::distance(hit->point, ghost.position);
            if (std::abs(glm::dot(hit->normal, ghost.up)) < 0.5f && distance < bestDistance) {
                best = hit;
                bestDistance = distance;
            }
        }
    }
    return best;
}

}

void tickVasskraka(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access) {
    const VasskrakaParams& p = def.vasskraka;
    const float size = vasskrakaSize(ghost, def);
    ghost.cooldown = std::max(0.0f, ghost.cooldown - dt);
    const glm::vec3 up = ghost.up;

    const GhostQuarry* quarry = nullptr;
    if (ghost.prefers != 255) {
        for (const GhostQuarry& player : context.players) {
            if (player.id == ghost.prefers && ghostPerceives(ghost, def, player, context)) {
                quarry = &player;
                ghost.lastKnown = player.feet;
                ghost.sinceSeen = 0.0f;
            }
        }
    }
    if (!quarry && ghost.perceives && ghost.target >= 0 && ghost.target < static_cast<int>(context.players.size())) {
        quarry = &context.players[static_cast<std::size_t>(ghost.target)];
    }
    ghost.quarryId = quarry ? quarry->id : static_cast<std::uint8_t>(255);

    if (ghost.dodging > 0.0f && ghost.state != GhostState::Scatter) {
        scatter(ghost, ghost.position + normalizeOr(ghost.velocity, up) * p.scatterDistance, access);
    }

    switch (ghost.state) {
    case GhostState::Scatter: {
        const float left = std::max(p.scatterTime - ghost.stateTime, dt);
        ghost.velocity = (ghost.goal - ghost.position) / left;
        if (ghost.stateTime >= p.scatterTime) {
            ghost.velocity = glm::vec3(0.0f);
            enter(ghost, GhostState::Circle);
            ghost.timer = std::max(ghost.timer, 0.8f);
        }
        break;
    }
    case GhostState::Stoop: {
        if (ghost.stateTime < p.diveWindup) {
            if (quarry) {
                ghost.goal = chestOf(*quarry);
            }
            const glm::vec3 back = normalizeOr(ghost.position - ghost.goal, up);
            ghost.velocity += ((back + up * 0.6f) * 1.2f - ghost.velocity) * std::min(1.0f, 8.0f * dt);
            break;
        }
        if (ghost.stateTime - dt < p.diveWindup) {
            ghost.velocity = normalizeOr(ghost.goal - ghost.position, -up) * p.diveSpeed;
        }

        if (!ghost.leapHit) {
            for (std::size_t i = 0; i < context.players.size(); ++i) {
                const GhostQuarry& player = context.players[i];
                if (distanceToSegment(ghost.position, player.feet, player.eye) < p.diveRadius * (0.6f + 0.4f * size)) {
                    const glm::vec3 along = normalizeOr(across(ghost.velocity, up), upBasis(up)[0]);
                    access.events.push_back(PlayerDamaged{p.diveDamage * size, ghost.position, static_cast<int>(i),
                                                          (along + player.up * 0.3f) * p.diveShove});
                    ghost.leapHit = true;
                    break;
                }
            }
        }

        const float sinceDive = ghost.stateTime - p.diveWindup;
        const float rising = glm::dot(ghost.velocity, up);
        const bool low = rising < 0.0f && castSurface(context, ghost.position, ghost.position - up * (def.radius + 0.25f)).has_value();
        const bool past = glm::dot(ghost.goal - ghost.position, ghost.velocity) < 0.0f && glm::distance(ghost.position, ghost.goal) > 2.5f;
        if (low || past || sinceDive > 1.4f) {
            if (low) {
                ghost.velocity += up * (std::abs(rising) * 0.3f - rising);
            }
            enter(ghost, GhostState::Circle);
            ghost.timer = nextAttack(p, access);
        }
        break;
    }
    case GhostState::Gather: {
        if (quarry) {
            ghost.goal = quarry->feet + quarry->up * p.ballHeight;
        }
        fly(ghost, ghost.goal, 9.0f, 6.0f, dt);
        const bool there = glm::length(across(ghost.position - ghost.goal, up)) < 0.6f;
        if ((ghost.stateTime >= p.gatherTime && there) || ghost.stateTime > p.gatherTime + 1.2f) {
            enter(ghost, GhostState::Fall);
            ghost.velocity = glm::vec3(0.0f);
        }
        break;
    }
    case GhostState::Fall: {
        ghost.velocity += ghostGravity(context, ghost.position) * dt;
        if (quarry) {
            const glm::vec3 toward = across(quarry->feet - ghost.position, up);
            ghost.velocity += normalizeOr(toward, glm::vec3(0.0f)) * (std::min(glm::length(toward), 1.0f) * p.fallLean * dt);
        }
        bool reached = false;
        for (const GhostQuarry& player : context.players) {
            reached = reached || distanceToSegment(ghost.position, player.feet, player.eye) < 0.7f;
        }
        const auto ground = castSurface(context, ghost.position, ghost.position + ghost.velocity * dt - up * (def.radius * 0.5f + 0.1f));
        const bool down = castSurface(context, ghost.position, ghost.position - up * (def.radius + 0.05f)).has_value();
        if (reached || ground || down || ghost.stateTime > 3.0f) {
            const float radius = p.burstRadius * (0.7f + 0.3f * size);
            const glm::vec3 center = ghost.position + up * 0.2f;
            for (std::size_t i = 0; i < context.players.size(); ++i) {
                const GhostQuarry& player = context.players[i];
                const glm::vec3 chest = chestOf(player);
                const float distance = distanceToSegment(center, player.feet, player.eye);
                if (distance < radius && !blocked(context, center, chest)) {
                    const glm::vec3 away = normalizeOr(across(chest - center, player.up), upBasis(player.up)[0]);
                    access.events.push_back(PlayerDamaged{p.burstDamage * size * (1.0f - 0.6f * distance / radius), center, static_cast<int>(i),
                                                          (away + player.up * 0.4f) * p.burstShove});
                }
            }
            access.events.push_back(KrakaBurst{ghost.id, center, radius});
            enter(ghost, GhostState::Reform);
            ghost.velocity = up * 2.5f;
        }
        break;
    }
    case GhostState::Reform:

        ghost.velocity += (up * 1.2f - ghost.velocity) * std::min(1.0f, 3.0f * dt);
        if (ghost.stateTime >= p.reformTime) {
            enter(ghost, GhostState::Circle);
            ghost.timer = nextAttack(p, access);
        }
        break;
    case GhostState::Merge: {
        Ghost* mate = nullptr;
        for (Ghost& other : access.ghosts()) {
            if (other.id == ghost.mate && other.state == GhostState::Merge && other.mate == ghost.id && !other.dying) {
                mate = &other;
            }
        }
        if (!mate || ghost.stateTime > 4.0f) {
            enter(ghost, GhostState::Circle);
            ghost.mate = 0;
            break;
        }
        if (ghost.hopsLeft == 1) {
            ghost.velocity *= std::exp(-4.0f * dt);
            break;
        }
        fly(ghost, mate->position, 8.0f, 6.0f, dt);
        if (glm::distance(ghost.position, mate->position) < 0.7f) {
            mate->health = std::min(mate->health + ghost.health, def.health * p.maxHealth);
            mate->mate = 0;
            mate->state = GhostState::Circle;
            mate->stateTime = 0.0f;
            mate->timer = nextAttack(p, access);
            access.events.push_back(KrakaMerged{mate->id, ghost.id, mate->position});
            access.remove(ghost);
        }
        break;
    }
    case GhostState::Circle: {
        if (!quarry) {
            if (ghost.sinceSeen > def.memory) {
                enter(ghost, GhostState::Roost);
                ghost.attached = false;
                ghost.prefers = 255;
                break;
            }
            fly(ghost, ghost.lastKnown + up * p.circleHeight, p.flySpeed, 2.0f, dt);
        } else {
            const glm::vec3 around = quarry->up;
            const glm::mat3 frame = upBasis(around);
            const glm::vec3 offset = across(ghost.position - quarry->feet, around);
            const float angle = std::atan2(glm::dot(offset, frame[2]), glm::dot(offset, frame[0])) + static_cast<float>(ghost.hopSide) * 0.7f;
            const glm::vec3 spot = quarry->feet + frame * glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * p.circleRadius +
                                   around * (p.circleHeight + 0.4f * std::sin(ghost.age * 1.3f + ghost.seed));

            const float off = std::abs(glm::length(offset) - p.circleRadius) +
                              std::abs(heightOf(ghost.position - quarry->feet, around) - p.circleHeight);
            fly(ghost, spot, glm::mix(p.circleSpeed, p.flySpeed, glm::smoothstep(1.5f, 5.0f, off)), 3.0f, dt);
            ghost.timer -= dt;
            const glm::vec3 chest = chestOf(*quarry);
            if (ghost.timer <= 0.0f && !blocked(context, ghost.position, chest)) {
                if (!ghost.lastBall && access.random01() < p.ballChance) {
                    ghost.lastBall = true;
                    enter(ghost, GhostState::Gather);
                    ghost.goal = quarry->feet + quarry->up * p.ballHeight;
                    access.events.push_back(KrakaBall{ghost.id, ghost.position});
                } else {
                    ghost.lastBall = false;
                    enter(ghost, GhostState::Stoop);
                    ghost.goal = chest;
                    ghost.leapHit = false;
                    access.events.push_back(KrakaDive{ghost.id, ghost.position});
                }
                break;
            }
        }

        if (ghost.health < def.health && ghost.stateTime > 1.0f) {
            for (Ghost& other : access.ghosts()) {
                if (other.id == ghost.id || other.type != ghost.type || other.state != GhostState::Circle || other.dying ||
                    other.health >= def.health || other.stateTime < 1.0f ||
                    glm::distance(other.position, ghost.position) > p.mergeRange) {
                    continue;
                }
                const bool stays = ghost.health >= other.health;
                ghost.mate = other.id;
                other.mate = ghost.id;
                ghost.hopsLeft = stays ? 1 : 0;
                other.hopsLeft = stays ? 0 : 1;
                enter(ghost, GhostState::Merge);
                other.state = GhostState::Merge;
                other.stateTime = 0.0f;
                break;
            }
        }
        break;
    }
    case GhostState::Roost:
    default: {
        if (ghost.state != GhostState::Roost) {
            enter(ghost, GhostState::Roost);
            ghost.attached = false;
        }
        if (quarry) {
            const glm::vec3 off = ghost.attached ? ghost.surfaceNormal : up;
            ghost.timer = p.firstAttack + nextAttack(p, access) * 0.3f;
            ghost.hopSide = access.random01() < 0.5f ? std::int8_t{1} : std::int8_t{-1};
            scatter(ghost, ghost.position + off * 1.2f + up * 0.8f, access);
            break;
        }
        if (ghost.attached) {
            ghost.velocity = glm::vec3(0.0f);
            break;
        }

        if (ghost.stateTime <= dt * 1.5f) {
            if (const auto wall = wallToRoostOn(ghost, p, context)) {
                ghost.goal = wall->point + wall->normal * (def.radius * 0.3f);
                ghost.surfaceNormal = wall->normal;
            } else {
                ghost.goal = ghost.position;
                ghost.surfaceNormal = up;
            }
        }
        fly(ghost, ghost.goal, p.returnSpeed, 4.0f, dt);
        if (glm::distance(ghost.position, ghost.goal) < 0.15f || ghost.stateTime > 8.0f) {
            ghost.position = glm::distance(ghost.position, ghost.goal) < 0.5f ? ghost.goal : ghost.position;
            ghost.velocity = glm::vec3(0.0f);
            ghost.attached = true;
        }
        break;
    }
    }
}

}
