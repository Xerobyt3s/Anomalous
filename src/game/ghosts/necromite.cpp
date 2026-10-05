#include "game/ghosts/necromite.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr float kReach = 40.0f;

void enter(Ghost& ghost, GhostState state) {
    ghost.state = state;
    ghost.stateTime = 0.0f;
}

bool usable(const GhostQuarry& player) { return player.downed && !player.possessed; }

}

void tickNecromite(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access) {
    const NecromiteParams& p = def.necromite;

    const GhostQuarry* body = nullptr;
    for (const GhostQuarry& player : context.players) {
        if (player.id == ghost.prefers && usable(player)) {
            body = &player;
        }
    }
    if (!body) {
        float nearest = kReach;
        for (const GhostQuarry& player : context.players) {
            const float distance = glm::distance(player.feet, ghost.position);
            if (usable(player) && distance < nearest) {
                nearest = distance;
                body = &player;
            }
        }
        ghost.prefers = body ? body->id : static_cast<std::uint8_t>(255);
    }
    ghost.quarryId = ghost.prefers;

    if (const auto ground = castSurface(context, ghost.position + kUp * 0.6f, ghost.position - kUp * 2.0f)) {
        ghost.position.y = ground->point.y + def.radius;
    }

    switch (ghost.state) {
    case GhostState::Squirm: {
        if (!body) {
            enter(ghost, GhostState::Burrow);
            ghost.velocity = glm::vec3(0.0f);
            break;
        }
        const glm::vec3 to{body->eye.x - ghost.position.x, 0.0f, body->eye.z - ghost.position.z};
        const float distance = glm::length(to);
        if (distance < p.enterRange) {
            enter(ghost, GhostState::Bore);
            ghost.velocity = glm::vec3(0.0f);
            break;
        }
        ghost.velocity = to / distance * p.crawlSpeed;
        ghost.position += ghost.velocity * dt;
        break;
    }
    case GhostState::Bore:
        ghost.velocity = glm::vec3(0.0f);
        if (!body || glm::distance(glm::vec2(body->eye.x, body->eye.z), glm::vec2(ghost.position.x, ghost.position.z)) > p.enterRange * 2.0f) {
            enter(ghost, GhostState::Squirm);
            break;
        }
        if (ghost.stateTime >= p.enterTime) {
            access.events.push_back(NecromiteEntered{ghost.id, body->id, 255, ghost.position});
            access.remove(ghost);
        }
        break;
    case GhostState::Burrow:
        ghost.velocity = glm::vec3(0.0f);
        if (ghost.stateTime >= p.emergeTime) {
            access.remove(ghost);
        }
        break;
    case GhostState::Emerge:
    default:
        if (ghost.state != GhostState::Emerge) {
            enter(ghost, GhostState::Emerge);
        }
        ghost.velocity = glm::vec3(0.0f);
        if (ghost.stateTime <= dt * 1.5f) {
            access.events.push_back(NecromiteEmerged{ghost.id, ghost.position});
        }
        if (ghost.stateTime >= p.emergeTime) {
            enter(ghost, GhostState::Squirm);
        }
        break;
    }
}

}
