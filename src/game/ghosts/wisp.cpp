#include "game/ghosts/wisp.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
void seek(Ghost& ghost, const glm::vec3& target, float speed, float response, float dt) {
    const glm::vec3 to = target - ghost.position;
    const float distance = glm::length(to);
    const glm::vec3 wanted = distance > 1e-3f ? to / distance * std::min(speed, distance * 3.0f) : glm::vec3(0.0f);
    ghost.velocity += (wanted - ghost.velocity) * std::min(1.0f, response * dt);
}

void enter(Ghost& ghost, GhostState state) {
    ghost.state = state;
    ghost.stateTime = 0.0f;
    ghost.awayTime = 0.0f;
}

}

void tickWisp(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access) {
    const WispParams& p = def.wisp;
    const float bob = 0.18f * std::sin(ghost.age * 1.7f + ghost.seed);
    const GhostQuarry* quarry =
        ghost.perceives && ghost.target >= 0 && ghost.target < static_cast<int>(context.players.size())
            ? &context.players[static_cast<std::size_t>(ghost.target)]
            : nullptr;

    switch (ghost.state) {
    case GhostState::Wander: {
        if (quarry) {
            enter(ghost, GhostState::Lure);
            break;
        }

        if (ghost.sinceSeen < def.memory) {
            seek(ghost, ghost.lastKnown + ghost.up * (p.hoverHeight + bob), p.lureSpeed, 2.0f, dt);
            break;
        }
        const float a = ghost.age * 0.23f + ghost.seed;
        const glm::vec3 around = upBasis(ghost.up) * glm::vec3(std::cos(a) * std::sin(a * 0.61f + 1.0f), 0.0f, std::sin(a * 1.31f));
        seek(ghost, ghost.home + around * p.wanderRadius + ghost.up * bob, p.wanderSpeed, 1.5f, dt);
        break;
    }
    case GhostState::Lure: {
        if (!quarry) {
            if (ghost.sinceSeen > def.memory) {
                ghost.home = ghost.position;
                enter(ghost, GhostState::Wander);
            } else {
                seek(ghost, ghost.lastKnown + ghost.up * (p.hoverHeight + bob), p.lureSpeed, 2.0f, dt);
            }
            break;
        }
        const glm::vec3 up = quarry->up;
        glm::vec3 away = across(ghost.position - quarry->feet, up);
        const float distance = glm::length(away);
        away = distance > 1e-3f ? away / distance : -upBasis(up)[2];

        const glm::vec3 side = glm::cross(up, away);
        const glm::vec3 spot = quarry->feet + away * p.lureDistance + side * (p.sway * std::sin(ghost.age * p.swaySpeed + ghost.seed)) +
                               up * (p.hoverHeight + bob);
        seek(ghost, spot, p.lureSpeed, 2.5f, dt);

        const glm::vec3 toWisp = glm::normalize(ghost.position - quarry->eye);
        const bool watched = glm::dot(quarry->viewDirection, toWisp) > 0.35f;
        ghost.awayTime = watched ? 0.0f : ghost.awayTime + dt;
        if (ghost.awayTime >= p.lookAwayTime || distance < p.rushTrigger) {
            enter(ghost, GhostState::Rush);
        }
        break;
    }
    case GhostState::Rush: {
        if (!quarry) {
            if (ghost.sinceSeen > 1.0f) {
                enter(ghost, GhostState::Lure);
            }
            break;
        }
        const glm::vec3 chest = glm::mix(quarry->feet, quarry->eye, 0.7f);
        const glm::vec3 to = chest - ghost.position;
        const float distance = glm::length(to);
        if (distance < def.radius + 0.6f) {
            for (std::size_t i = 0; i < context.players.size(); ++i) {
                const GhostQuarry& near = context.players[i];
                if (glm::distance(glm::mix(near.feet, near.eye, 0.7f), ghost.position) < p.burstRadius) {
                    access.events.push_back(PlayerDamaged{p.burstDamage, ghost.position, static_cast<int>(i)});
                }
            }
            access.events.push_back(GhostBurst{ghost.id, ghost.position, p.burstRadius});
            access.burst(ghost);
            break;
        }

        if (distance > p.rushTrigger && glm::dot(quarry->viewDirection, glm::normalize(ghost.position - quarry->eye)) > 0.35f) {
            ghost.velocity *= 0.3f;
            enter(ghost, GhostState::Lure);
            break;
        }
        const glm::vec3 wanted = to / std::max(distance, 1e-3f) * p.rushSpeed;
        ghost.velocity += (wanted - ghost.velocity) * std::min(1.0f, 6.0f * dt);
        if (ghost.stateTime > 4.0f) {
            enter(ghost, GhostState::Lure);
        }
        break;
    }
    case GhostState::Lift:
    case GhostState::Recover:
        ghost.state = GhostState::Wander;
        break;
    case GhostState::Flinch: {
        ghost.velocity *= std::exp(-2.5f * dt);
        if (ghost.stateTime >= p.flinchTime) {
            enter(ghost, quarry ? GhostState::Lure : GhostState::Wander);
        }
        break;
    }
    }
}

}
