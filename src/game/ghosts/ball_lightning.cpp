#include "game/ghosts/ball_lightning.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
void enter(Ghost& ghost, GhostState state) {
    ghost.state = state;
    ghost.stateTime = 0.0f;
}

float groundBelow(const GhostContext& context, const glm::vec3& at, const glm::vec3& up, float hover) {
    const auto hit = castSurface(context, at + up * 2.0f, at - up * 30.0f);
    return hit ? heightOf(hit->point, up) : heightOf(at, up) - hover;
}

glm::vec3 atHeight(const glm::vec3& point, const glm::vec3& up, float height) {
    return point + up * (height - heightOf(point, up));
}

glm::vec3 hopDestination(Ghost& ghost, const GhostDef& def, const GhostContext& context, const GhostQuarry* quarry,
                         GhostAccess& access) {
    const BallLightningParams& p = def.ballLightning;
    const float r = access.random01();
    auto clear = [&](const glm::vec3& to) { return !context.blocked || !context.blocked(ghost.position, to); };

    const glm::vec3 up = ghost.up;
    const glm::mat3 frame = upBasis(up);
    if (quarry) {
        const glm::mat3 around = upBasis(quarry->up);
        const glm::vec3 offset = ghost.position - quarry->feet;
        const glm::vec2 rel{glm::dot(offset, around[0]), glm::dot(offset, around[2])};
        if (glm::length(rel) < p.circleRange) {
            const float radius = p.circleRadius * (0.85f + 0.3f * access.random01());
            const float height = 1.2f + 1.0f * access.random01();
            const float from = std::atan2(rel.y, rel.x);
            for (int attempt = 0; attempt < 2; ++attempt) {
                const float angle = from + static_cast<float>(ghost.hopSide) * (0.9f + 0.5f * r);
                const glm::vec3 to = quarry->feet + around * glm::vec3(std::cos(angle) * radius, height, std::sin(angle) * radius);
                if (clear(to)) {
                    return to;
                }
                ghost.hopSide = static_cast<std::int8_t>(-ghost.hopSide);
            }
            return ghost.position;
        }
    }

    glm::vec3 goal = ghost.home + frame * glm::vec3(std::cos(ghost.age + ghost.seed), 0.0f, std::sin(ghost.age + ghost.seed)) * 3.0f;
    if (quarry) {
        goal = quarry->feet;
    } else if (ghost.sinceSeen < def.memory) {
        goal = ghost.lastKnown;
    }
    const glm::vec3 to = across(goal - ghost.position, up);
    const float distance = glm::length(to);
    const glm::vec3 ahead = distance > 0.1f ? to / distance : frame * glm::vec3(std::cos(ghost.seed), 0.0f, std::sin(ghost.seed));
    const glm::vec3 side = glm::normalize(glm::cross(ahead, up));
    const float step = glm::mix(p.hopMin, p.hopMax, r);
    const float forward = std::min(step * 0.8f, distance);
    for (int attempt = 0; attempt < 3; ++attempt) {
        const float zig = attempt == 1 ? -1.0f : 1.0f;
        const float shrink = attempt == 2 ? 0.4f : 1.0f;
        glm::vec3 dest = ghost.position + (ahead * forward + side * (step * 0.6f * static_cast<float>(ghost.hopSide) * zig)) * shrink;
        dest = atHeight(dest, up, groundBelow(context, dest, up, p.hoverHeight) + p.hoverHeight + (access.random01() - 0.5f) * 0.8f);
        if (clear(dest)) {
            ghost.hopSide = static_cast<std::int8_t>(-ghost.hopSide * static_cast<int>(zig));
            return dest;
        }
    }
    return ghost.position;
}

void throwVolley(Ghost& ghost, const GhostDef& def, const GhostContext& context, GhostAccess& access) {
    const BallLightningParams& p = def.ballLightning;
    for (int k = 0; k < p.arcsPerVolley; ++k) {
        glm::vec4* slot = nullptr;
        for (glm::vec4& arc : ghost.arcs) {
            if (arc.w <= 0.0f) {
                slot = &arc;
                break;
            }
        }
        if (!slot) {
            return;
        }

        const float angle = access.random01() * 6.2831853f;
        const glm::vec3 direction = glm::normalize(upBasis(ghost.up) * glm::vec3(std::cos(angle), -(0.25f + 0.75f * access.random01()), std::sin(angle)));
        if (const auto hit = castSurface(context, ghost.position, ghost.position + direction * p.arcReach)) {
            *slot = glm::vec4(hit->point, p.arcDelay);
            access.events.push_back(BallArcCharged{ghost.id, hit->point, p.arcDelay});
        }
    }
}

}

void tickBallLightning(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access) {
    const BallLightningParams& p = def.ballLightning;
    const GhostQuarry* quarry =
        ghost.perceives && ghost.target >= 0 && ghost.target < static_cast<int>(context.players.size())
            ? &context.players[static_cast<std::size_t>(ghost.target)]
            : nullptr;

    for (glm::vec4& arc : ghost.arcs) {
        if (arc.w <= 0.0f) {
            continue;
        }
        arc.w -= dt;
        if (arc.w > 0.0f) {
            continue;
        }
        arc.w = 0.0f;
        const glm::vec3 at{arc};
        access.events.push_back(BallArc{ghost.id, ghost.position, at});
        for (std::size_t i = 0; i < context.players.size(); ++i) {
            const GhostQuarry& player = context.players[i];
            if (!player.hidden && distanceToSegment(at, player.feet, player.eye) < p.arcRadius) {
                access.events.push_back(PlayerDamaged{p.arcDamage, at, static_cast<int>(i)});
            }
        }
    }

    switch (ghost.state) {
    case GhostState::Hop: {
        ghost.velocity = glm::vec3(0.0f);
        ghost.timer -= dt;
        if (ghost.timer > 0.0f) {
            break;
        }
        if (ghost.hopsLeft == 0) {
            enter(ghost, GhostState::Charge);
            ghost.timer = p.arcEvery * 0.5f;
            break;
        }
        const glm::vec3 from = ghost.position;
        const glm::vec3 to = hopDestination(ghost, def, context, quarry, access);
        if (glm::distance(from, to) > 0.05f) {
            ghost.position = to;
            access.events.push_back(BallHopped{ghost.id, from, to});
        }
        --ghost.hopsLeft;
        ghost.timer = p.hopEvery;
        break;
    }
    case GhostState::Flinch:

        ghost.velocity *= std::exp(-5.0f * dt);
        if (ghost.stateTime >= p.flinchTime) {
            enter(ghost, GhostState::Hop);
            ghost.hopsLeft = static_cast<std::uint8_t>(std::max(p.hops, 1));
            ghost.timer = 0.0f;
            ghost.arcs = {};
        }
        break;
    case GhostState::Charge:
    case GhostState::Wander:
    default: {
        const bool aware = ghost.perceives || ghost.sinceSeen < def.memory;
        const glm::vec3 up = ghost.up;
        const glm::mat3 frame = upBasis(up);
        const float height = groundBelow(context, ghost.position, up, p.hoverHeight) + p.hoverHeight + 0.15f * std::sin(ghost.age * 2.3f + ghost.seed);
        const float rise = (height - heightOf(ghost.position, up)) * 2.0f;
        if (!aware) {
            if (ghost.state != GhostState::Wander) {
                enter(ghost, GhostState::Wander);
                ghost.arcs = {};
            }
            const glm::vec3 to = ghost.home + frame * glm::vec3(std::cos(ghost.age * 0.15f + ghost.seed), 0.0f, std::sin(ghost.age * 0.19f + ghost.seed * 2.0f)) * p.wanderRadius -
                                 ghost.position;
            glm::vec3 wanted = across(to, up);
            const float distance = glm::length(wanted);
            if (distance > p.wanderSpeed) {
                wanted *= p.wanderSpeed / distance;
            }
            wanted += up * rise;
            ghost.velocity += (wanted - ghost.velocity) * std::min(1.0f, 2.0f * dt);
            break;
        }
        if (ghost.state != GhostState::Charge) {
            enter(ghost, GhostState::Charge);
            ghost.timer = p.arcEvery * 0.5f;
        }

        const glm::vec3 drift = frame * glm::vec3(std::cos(ghost.age * 0.23f + ghost.seed), 0.0f, std::sin(ghost.age * 0.31f + ghost.seed * 1.7f)) * p.driftSpeed;
        const glm::vec3 wanted = drift + up * rise;
        ghost.velocity += (wanted - ghost.velocity) * std::min(1.0f, 4.0f * dt);
        ghost.timer -= dt;
        if (ghost.timer <= 0.0f) {
            throwVolley(ghost, def, context, access);
            ghost.timer = p.arcEvery * (0.7f + 0.6f * access.random01());
        }
        if (ghost.stateTime >= p.chargeTime) {
            enter(ghost, GhostState::Hop);
            ghost.hopsLeft = static_cast<std::uint8_t>(std::max(p.hops, 1));
            ghost.timer = 0.0f;
            ghost.arcs = {};
            ghost.velocity = glm::vec3(0.0f);
        }
        break;
    }
    }
}

}
