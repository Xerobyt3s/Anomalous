#include "game/ghosts/poltergeist.h"

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
}

const GhostProp* findProp(const GhostContext& context, std::uint32_t body) {
    for (const GhostProp& prop : context.props) {
        if (prop.body == body) {
            return &prop;
        }
    }
    return nullptr;
}

const GhostProp* nearestProp(const GhostContext& context, const glm::vec3& from, float within) {
    const GhostProp* best = nullptr;
    float bestDistance = within;
    for (const GhostProp& prop : context.props) {
        const float distance = glm::distance(prop.position, from);
        if (distance < bestDistance) {
            best = &prop;
            bestDistance = distance;
        }
    }
    return best;
}

}

void tickPoltergeist(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access) {
    const PoltergeistParams& p = def.poltergeist;
    const float bob = 0.12f * std::sin(ghost.age * 1.3f + ghost.seed);
    const GhostQuarry* quarry =
        ghost.perceives && ghost.target >= 0 && ghost.target < static_cast<int>(context.players.size())
            ? &context.players[static_cast<std::size_t>(ghost.target)]
            : nullptr;

    const float a = ghost.age * 0.31f + ghost.seed;
    const glm::vec3 drift{std::cos(a) * std::sin(a * 0.7f + 1.0f), 0.0f, std::sin(a * 1.2f)};
    const glm::vec3 haunt = ghost.home + drift * p.wanderRadius + glm::vec3(0.0f, bob, 0.0f);

    if (ghost.state != GhostState::Lift && ghost.heldProp != kNoProp) {
        ghost.heldProp = kNoProp;
    }

    switch (ghost.state) {
    case GhostState::Wander:
        seek(ghost, haunt, p.speed * 0.5f, 1.5f, dt);
        if (quarry) {
            enter(ghost, GhostState::Lure);
        }
        break;

    case GhostState::Lure: {
        if (!quarry) {
            seek(ghost, haunt, p.speed * 0.5f, 1.5f, dt);
            if (ghost.sinceSeen > def.memory) {
                enter(ghost, GhostState::Wander);
            }
            break;
        }
        if (const GhostProp* prop = nearestProp(context, ghost.position, p.grabRange)) {
            seek(ghost, haunt, p.speed, 2.0f, dt);
            ghost.heldProp = prop->body;
            enter(ghost, GhostState::Lift);
        } else if (const GhostProp* far = nearestProp(context, ghost.position, p.grabRange * 4.0f)) {
            seek(ghost, far->position + glm::vec3(0.0f, p.hoverHeight, 0.0f), p.speed, 2.0f, dt);
            if (glm::distance(ghost.position, far->position) < p.grabRange) {
                ghost.home = ghost.position;
            }
        } else {
            seek(ghost, haunt, p.speed, 2.0f, dt);
        }
        break;
    }

    case GhostState::Lift: {
        seek(ghost, haunt, p.speed * 0.3f, 2.0f, dt);
        const GhostProp* prop = findProp(context, ghost.heldProp);
        if (!prop || !quarry) {
            ghost.heldProp = kNoProp;
            enter(ghost, quarry ? GhostState::Recover : GhostState::Lure);
            break;
        }
        const glm::vec3 chest = glm::mix(quarry->feet, quarry->eye, 0.7f);
        if (ghost.stateTime < p.holdTime) {
            const glm::vec3 hold{prop->position.x, std::max(prop->position.y, 0.0f), prop->position.z};
            const float lift = p.liftHeight * std::min(1.0f, ghost.stateTime / (p.holdTime * 0.6f));
            const glm::vec3 target{ghost.liftFrom.x, ghost.liftFrom.y + lift, ghost.liftFrom.z};
            if (ghost.stateTime <= dt * 1.5f) {
                ghost.liftFrom = hold;
            }
            const glm::vec3 shake{std::sin(ghost.age * 47.0f), std::sin(ghost.age * 53.0f + 1.0f), std::sin(ghost.age * 41.0f + 2.0f)};
            glm::vec3 velocity = (target - prop->position) * 7.0f + shake * 0.6f;
            if (glm::length(velocity) > 6.0f) {
                velocity = glm::normalize(velocity) * 6.0f;
            }
            if (context.moveProp) {
                context.moveProp(prop->body, velocity);
            }
        } else {
            const glm::vec3 to = chest - prop->position;
            const float distance = glm::length(to);
            const float flight = distance / std::max(p.throwSpeed, 1.0f);
            const glm::vec3 velocity = (distance > 1e-3f ? to / distance : glm::vec3(0.0f, 0.0f, -1.0f)) * p.throwSpeed +
                                       glm::vec3(0.0f, 0.5f * 9.81f * flight, 0.0f);
            if (context.moveProp) {
                context.moveProp(prop->body, velocity);
            }
            access.events.push_back(GhostThrew{ghost.id, prop->body, prop->position, velocity});
            ghost.heldProp = kNoProp;
            enter(ghost, GhostState::Recover);
        }
        break;
    }

    case GhostState::Recover:
        seek(ghost, haunt, p.speed, 2.0f, dt);
        if (ghost.stateTime >= p.cooldown) {
            enter(ghost, GhostState::Lure);
        }
        break;

    case GhostState::Flinch:
        ghost.velocity *= std::exp(-2.5f * dt);
        if (ghost.stateTime >= p.flinchTime) {
            enter(ghost, GhostState::Lure);
        }
        break;

    case GhostState::Rush:
        enter(ghost, GhostState::Lure);
        break;
    }
}

}
