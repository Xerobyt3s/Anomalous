#include "game/ghosts/ghost_world.h"

#include "game/ghosts/ball_lightning.h"
#include "game/ghosts/mimic.h"
#include "game/ghosts/necromite.h"
#include "game/ghosts/vasskraka.h"
#include "game/ghosts/poltergeist.h"
#include "game/ghosts/wisp.h"
#include "game/world/element_volume.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr float kUpEase = 4.0f;

void keepAboveFloor(Ghost& ghost, const GhostDef& def, const GhostContext& context) {
    if (context.raycast) {
        const glm::vec3 above = ghost.position + ghost.up * def.radius;
        if (const auto floor = context.raycast(above, ghost.position - ghost.up * def.radius)) {
            const float clearance = glm::dot(ghost.position - floor->point, floor->normal);
            if (clearance < def.radius) {
                ghost.position += floor->normal * (def.radius - clearance);
                const float into = glm::dot(ghost.velocity, floor->normal);
                if (into < 0.0f) {
                    ghost.velocity -= floor->normal * into;
                }
            }
        }
    } else if (ghost.position.y < def.radius) {
        ghost.position.y = def.radius;
        ghost.velocity.y = std::max(ghost.velocity.y, 0.0f);
    }
}
}
namespace {
constexpr float kFogBlind = 0.25f;
constexpr float kSourceMemory = 0.5f;

}

const char* ghostStateName(GhostState state) {
    switch (state) {
    case GhostState::Charge:
        return "charge";
    case GhostState::Hop:
        return "hop";
    case GhostState::Disguised:
        return "disguised";
    case GhostState::Reveal:
        return "reveal";
    case GhostState::Hunt:
        return "hunt";
    case GhostState::Pause:
        return "pause";
    case GhostState::Leap:
        return "leap";
    case GhostState::Flee:
        return "flee";
    case GhostState::Conceal:
        return "conceal";
    case GhostState::Roost:
        return "roost";
    case GhostState::Circle:
        return "circle";
    case GhostState::Stoop:
        return "stoop";
    case GhostState::Gather:
        return "gather";
    case GhostState::Fall:
        return "fall";
    case GhostState::Reform:
        return "reform";
    case GhostState::Scatter:
        return "scatter";
    case GhostState::Merge:
        return "merge";
    case GhostState::Emerge:
        return "emerge";
    case GhostState::Squirm:
        return "squirm";
    case GhostState::Bore:
        return "bore";
    case GhostState::Burrow:
        return "burrow";
    case GhostState::Windup:
        return "windup";
    case GhostState::Lift:
        return "lift";
    case GhostState::Recover:
        return "recover";
    case GhostState::Lure:
        return "lure";
    case GhostState::Rush:
        return "rush";
    case GhostState::Flinch:
        return "flinch";
    case GhostState::Wander:
    default:
        return "wander";
    }
}

float GhostAccess::random01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(world.m_rng); }
std::vector<Ghost>& GhostAccess::ghosts() { return world.m_ghosts; }
void GhostAccess::remove(const Ghost& ghost) { world.m_dead.push_back(ghost.id); }

float vasskrakaSize(const Ghost& ghost, const GhostDef& def) {
    return std::clamp(ghost.health / std::max(def.health, 1e-3f), def.vasskraka.minSize, def.vasskraka.maxHealth);
}

bool ghostPerceives(const Ghost& ghost, const GhostDef& def, const GhostQuarry& quarry, const GhostContext& context) {
    if (quarry.hidden) {
        return false;
    }
    const glm::vec3 chest = glm::mix(quarry.feet, quarry.eye, 0.7f);
    if (glm::distance(ghost.position, chest) > def.sightRange * quarry.visibility) {
        return false;
    }
    if (!def.seesThroughWalls && context.blocked && context.blocked(ghost.position, chest)) {
        return false;
    }
    if (context.fog && context.fog->transmittance(ghost.position, chest) < kFogBlind) {
        return false;
    }
    return true;
}

glm::vec3 ghostGravity(const GhostContext& context, const glm::vec3& at) {
    return context.gravity ? context.gravity(at) : glm::vec3(0.0f, -9.81f, 0.0f);
}

glm::vec3 ghostUp(const GhostContext& context, const glm::vec3& at) {
    const glm::vec3 g = ghostGravity(context, at);
    const float length = glm::length(g);
    return length > 1e-4f ? -g / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

glm::mat3 upBasis(const glm::vec3& up) {
    const glm::vec3 helper = std::abs(up.z) < 0.9f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(up, helper));
    return glm::mat3(right, up, glm::cross(right, up));
}

bool ghostAttacking(const Ghost& ghost, const GhostDef& def) {
    switch (def.behavior) {
    case GhostBehavior::Wisp: return ghost.state == GhostState::Rush;
    case GhostBehavior::BallLightning: return ghost.state == GhostState::Charge;
    case GhostBehavior::Mimic: return ghost.state == GhostState::Windup || ghost.state == GhostState::Leap;
    case GhostBehavior::Vasskraka: return ghost.state == GhostState::Stoop;
    case GhostBehavior::Poltergeist: return ghost.state == GhostState::Lift;
    case GhostBehavior::Necromite: return ghost.state == GhostState::Bore;
    }
    return false;
}

std::optional<GhostSurfaceHit> castSurface(const GhostContext& context, const glm::vec3& from, const glm::vec3& to) {
    if (context.raycast) {
        return context.raycast(from, to);
    }
    if ((from.y >= 0.0f) == (to.y >= 0.0f) || std::abs(from.y - to.y) < 1e-6f) {
        return std::nullopt;
    }
    const float t = from.y / (from.y - to.y);
    return GhostSurfaceHit{glm::mix(from, to, t), from.y >= 0.0f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f)};
}

GhostHitSphere ghostHitSphere(const Ghost& ghost, const GhostDef& def) {
    if (def.behavior == GhostBehavior::Vasskraka) {
        return {ghost.position, def.radius * (0.5f + 0.5f * vasskrakaSize(ghost, def))};
    }
    if (def.behavior != GhostBehavior::Mimic) {
        return {ghost.position, def.radius};
    }
    if (ghost.state == GhostState::Disguised) {
        return {ghost.position, def.mimic.disguisedRadius};
    }
    return {ghost.position + ghost.surfaceNormal * (ghost.attached ? def.mimic.standHeight : 0.0f), def.radius};
}

std::uint32_t GhostWorld::spawn(GhostTypeId type, const glm::vec3& position, const glm::vec3& up) {
    Ghost ghost;
    ghost.id = m_nextId++;
    ghost.type = type;
    ghost.position = position;
    ghost.up = up;
    ghost.home = position;
    ghost.health = m_data->types[type].health;
    ghost.seed = std::uniform_real_distribution<float>(0.0f, 100.0f)(m_rng);
    const GhostDef& def = m_data->types[type];
    if (def.behavior == GhostBehavior::Mimic) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        ghost.disguise = pickDisguise(def, unit(m_rng), unit(m_rng));
        ghost.state = GhostState::Disguised;
    } else if (def.behavior == GhostBehavior::BallLightning) {
        ghost.state = GhostState::Charge;
    } else if (def.behavior == GhostBehavior::Vasskraka) {
        ghost.state = GhostState::Roost;
        ghost.attached = false;
    } else if (def.behavior == GhostBehavior::Necromite) {
        ghost.state = GhostState::Emerge;
    }
    m_ghosts.push_back(ghost);
    return ghost.id;
}

const Ghost* GhostWorld::find(std::uint32_t id) const {
    const auto it = std::find_if(m_ghosts.begin(), m_ghosts.end(), [id](const Ghost& g) { return g.id == id; });
    return it == m_ghosts.end() ? nullptr : &*it;
}

void GhostWorld::sense(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt) {
    ghost.perceives = false;
    ghost.sinceSeen += dt;
    float nearest = 1e9f;
    for (std::size_t i = 0; i < context.players.size(); ++i) {
        const GhostQuarry& quarry = context.players[i];
        if (!ghostPerceives(ghost, def, quarry, context)) {
            continue;
        }
        const float distance = glm::distance(ghost.position, quarry.eye);
        if (distance < nearest) {
            nearest = distance;
            ghost.perceives = true;
            ghost.target = static_cast<int>(i);
            ghost.lastKnown = quarry.feet;
            ghost.sinceSeen = 0.0f;
        }
    }
}

void GhostWorld::tick(float dt, const GhostContext& context, EventList& events) {
    GhostAccess access{*this, events};
    for (Ghost& ghost : m_ghosts) {
        const GhostDef& def = m_data->types[ghost.type];
        for (GhostHitSource& source : ghost.sources) {
            source.timeLeft = std::max(0.0f, source.timeLeft - dt);
        }

        if (ghost.hitstop > 0.0f) {
            ghost.hitstop -= dt;
            if (ghost.hitstop > 0.0f) {
                continue;
            }
            ghost.hitstop = 0.0f;
        }
        if (ghost.posed) {
            ghost.age += dt;
            ghost.stateTime += dt;
            if (ghost.poseLoop > 0.0f && ghost.stateTime >= ghost.poseLoop) {
                ghost.stateTime = 0.0f;
            }
            continue;
        }
        if (ghost.dying) {
            kill(ghost, false, events);
            continue;
        }
        ghost.age += dt;
        ghost.stateTime += dt;
        ghost.dodging = std::max(0.0f, ghost.dodging - dt);
        const glm::vec3 wantUp = ghostUp(context, ghost.position);
        const glm::vec3 eased = ghost.up + (wantUp - ghost.up) * std::min(1.0f, kUpEase * dt);
        ghost.up = glm::length(eased) > 1e-3f ? glm::normalize(eased) : wantUp;
        sense(ghost, def, context, dt);
        if (tumble(ghost, def, context, dt, events)) {
            continue;
        }

        switch (def.behavior) {
        case GhostBehavior::BallLightning:
            tickBallLightning(ghost, def, context, dt, access);
            break;
        case GhostBehavior::Mimic:
            tickMimic(ghost, def, context, dt, access);
            break;
        case GhostBehavior::Necromite:
            tickNecromite(ghost, def, context, dt, access);
            break;
        case GhostBehavior::Vasskraka:
            tickVasskraka(ghost, def, context, dt, access);
            break;
        case GhostBehavior::Poltergeist:
            tickPoltergeist(ghost, def, context, dt, access);
            break;
        case GhostBehavior::Wisp:
            tickWisp(ghost, def, context, dt, access);
            break;
        }

        if (def.behavior == GhostBehavior::Vasskraka && ghost.state == GhostState::Roost && ghost.attached) {
            continue;
        }
        if (def.behavior == GhostBehavior::Necromite) {
            continue;
        }
        if (def.behavior == GhostBehavior::Mimic) {
            continue;
        }

        ghost.position += ghost.velocity * dt;
        if (context.raycast) {
            const glm::vec3 above = ghost.position + ghost.up * def.radius;
            if (const auto floor = context.raycast(above, ghost.position - ghost.up * def.radius)) {
                const float clearance = glm::dot(ghost.position - floor->point, floor->normal);
                if (clearance < def.radius) {
                    ghost.position += floor->normal * (def.radius - clearance);
                }
            }
        } else {
            ghost.position.y = std::max(ghost.position.y, def.radius);
        }
    }
    std::erase_if(m_ghosts, [this](const Ghost& g) {
        return std::find(m_dead.begin(), m_dead.end(), g.id) != m_dead.end();
    });
    m_dead.clear();
}

bool GhostWorld::tumble(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, EventList& events) {
    if (def.windImmune || !context.wind) {
        ghost.caught = 0.0f;
        return false;
    }
    const CaughtTuning& t = m_data->caught;
    const glm::vec3 air = context.wind(ghost.position);
    const bool strong = glm::length(air) > t.catchSpeed;
    if (ghost.caught <= 0.0f) {
        if (!strong || ghost.state == GhostState::Scatter || ghost.state == GhostState::Hop) {
            return false;
        }
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        if (def.behavior == GhostBehavior::Mimic) {
            if (ghost.state == GhostState::Disguised || ghost.state == GhostState::Conceal) {
                events.push_back(MimicRevealed{ghost.id, ghost.position, ghost.disguise});
            }
            ghost.state = GhostState::Flinch;
            ghost.stateTime = 0.0f;
        }
        ghost.heldProp = kNoProp;
        ghost.attached = false;
        ghost.calm = 0.0f;
        glm::vec3 axis{unit(m_rng), unit(m_rng), unit(m_rng)};
        axis = glm::length(axis) > 1e-3f ? glm::normalize(axis) : glm::vec3(1.0f, 0.0f, 0.0f);
        ghost.spin = axis * (t.tumbleSpin * (0.6f + 0.4f * std::abs(unit(m_rng))));
        ghost.caught = dt * 0.5f;
    }
    ghost.caught += dt;
    ghost.calm = strong ? 0.0f : ghost.calm + dt;
    if (ghost.calm >= t.releaseTime) {
        release(ghost, def);
        return false;
    }
    const float grip = std::min(t.grip * (1.0f - def.weight) * dt, 1.0f);
    ghost.velocity += (air - ghost.velocity) * grip;
    const bool limp = def.behavior == GhostBehavior::Mimic;
    ghost.velocity += ghostGravity(context, ghost.position) * ((limp ? 1.0f : t.fallShare) * dt);
    ghost.position += ghost.velocity * dt;
    keepAboveFloor(ghost, def, context);
    if (limp) {
        const glm::vec3 turned = ghost.surfaceNormal + glm::cross(ghost.spin, ghost.surfaceNormal) * dt;
        ghost.surfaceNormal = glm::length(turned) > 1e-4f ? glm::normalize(turned) : ghost.up;
        ghost.spin *= std::exp(-0.3f * dt);
    }
    return true;
}

void GhostWorld::release(Ghost& ghost, const GhostDef& def) {
    ghost.caught = 0.0f;
    ghost.calm = 0.0f;
    ghost.spin = glm::vec3(0.0f);
    ghost.stateTime = 0.0f;
    switch (def.behavior) {
    case GhostBehavior::Vasskraka:
        ghost.state = GhostState::Circle;
        ghost.timer = std::max(ghost.timer, 0.8f);
        break;
    case GhostBehavior::Mimic:
        ghost.state = GhostState::Flinch;
        if (glm::dot(ghost.surfaceNormal, ghost.up) < 0.0f) {
            ghost.surfaceNormal = ghost.up;
        }
        break;
    default:
        ghost.state = GhostState::Flinch;
        break;
    }
}

std::optional<GhostRayHit> GhostWorld::raycast(const glm::vec3& from, const glm::vec3& to) const {
    std::optional<GhostRayHit> best;
    for (const Ghost& ghost : m_ghosts) {
        if (ghost.dodging > 0.0f || ghost.dying || ghostUntouchable(ghost)) {
            continue;
        }
        const GhostHitSphere sphere = ghostHitSphere(ghost, m_data->types[ghost.type]);
        const auto t = segmentSphere(from, to, sphere.center, sphere.radius);
        if (t && (!best || *t < best->fraction)) {
            GhostRayHit hit;
            hit.id = ghost.id;
            hit.fraction = *t;
            hit.point = glm::mix(from, to, *t);
            const glm::vec3 out = hit.point - sphere.center;
            hit.normal = glm::length(out) > 1e-4f ? glm::normalize(out) : glm::normalize(from - to);
            best = hit;
        }
    }
    return best;
}

void GhostWorld::kill(Ghost& ghost, bool burst, EventList& events) {
    if (std::find(m_dead.begin(), m_dead.end(), ghost.id) != m_dead.end()) {
        return;
    }
    m_dead.push_back(ghost.id);
    events.push_back(GhostDied{ghost.id, ghost.type, ghost.position, burst, ghost.disguise});
}

bool GhostWorld::damage(std::uint32_t id, float rounds, DamageKind kind, const glm::vec3& point, EventList& events,
                        std::uint32_t source, std::uint8_t attacker) {
    const auto it = std::find_if(m_ghosts.begin(), m_ghosts.end(), [id](const Ghost& g) { return g.id == id; });
    if (it == m_ghosts.end() || it->dying || std::find(m_dead.begin(), m_dead.end(), id) != m_dead.end()) {
        return false;
    }
    Ghost& ghost = *it;
    const GhostDef& def = m_data->types[ghost.type];

    bool hit = source == 0;
    if (source != 0) {
        GhostHitSource* slot = &ghost.sources[0];
        bool known = false;
        for (GhostHitSource& s : ghost.sources) {
            if (s.id == source && s.timeLeft > 0.0f) {
                slot = &s;
                known = true;
                break;
            }
            if (s.timeLeft < slot->timeLeft) {
                slot = &s;
            }
        }
        *slot = {source, kSourceMemory};
        hit = !known;
    }

    const float amount = rounds * m_data->roundDamage * def.damage[static_cast<std::size_t>(kind)];
    ghost.health -= amount;
    const bool killed = ghost.health <= 0.0f;
    if (hit || killed) {
        if (ghost.hitstop <= 0.0f || killed) {
            const float weight = killed ? 1.0f : std::clamp(amount / std::max(def.health, 1e-3f), 0.0f, 1.0f);
            ghost.hitstop = std::max(ghost.hitstop, glm::mix(def.hitstopMin, def.hitstopMax, weight));
        }
        events.push_back(GhostHurt{ghost.id, point, amount, kind, killed, ghost.hitstop});
    }
    if (killed) {
        ghost.dying = true;
    } else if (def.behavior == GhostBehavior::Mimic && ghost.state == GhostState::Disguised) {
        ghost.state = GhostState::Reveal;
        ghost.stateTime = 0.0f;
        events.push_back(MimicRevealed{ghost.id, ghost.position, ghost.disguise});
    } else if (def.behavior == GhostBehavior::Mimic && source == 0) {
        ghost.state = GhostState::Flinch;
        ghost.stateTime = 0.0f;
        if (ghost.attached) {
            ghost.velocity = glm::vec3(0.0f);
        }
    } else if (def.behavior == GhostBehavior::Vasskraka) {
        if (source == 0 && ghost.state != GhostState::Scatter && ghost.caught <= 0.0f) {
            const glm::vec3 away = ghost.position - point;
            std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
            const glm::mat3 frame = upBasis(ghost.up);
            const glm::vec3 aside = glm::normalize((glm::length(away) > 1e-3f ? glm::normalize(away) : ghost.up) +
                                                   frame * glm::vec3(unit(m_rng), 0.3f + 0.4f * std::abs(unit(m_rng)), unit(m_rng)));
            ghost.goal = ghost.position + aside * def.vasskraka.scatterDistance;
            ghost.state = GhostState::Scatter;
            ghost.stateTime = 0.0f;
            ghost.attached = false;
            events.push_back(KrakaScattered{ghost.id, ghost.position});
        }
    } else if (source == 0) {
        ghost.state = GhostState::Flinch;
        ghost.stateTime = 0.0f;
        const glm::vec3 away = ghost.position - point;
        std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
        ghost.velocity = glm::normalize((glm::length(away) > 1e-3f ? glm::normalize(away) : ghost.up) +
                                        upBasis(ghost.up) * glm::vec3(unit(m_rng), unit(m_rng) * 0.5f, unit(m_rng))) * 6.0f;
    }

    if (def.behavior == GhostBehavior::Vasskraka && !killed && attacker != 255 && ghost.quarryId != 255 && attacker != ghost.quarryId &&
        ghost.cooldown <= 0.0f && ghost.health > def.vasskraka.splitAbove * def.health) {
        ghost.health *= 0.5f;
        ghost.cooldown = def.vasskraka.splitCooldown;
        Ghost half = ghost;
        half.id = m_nextId++;
        half.prefers = attacker;
        half.quarryId = attacker;
        half.seed += 37.0f;
        half.sources = {};
        half.hitstop = 0.0f;
        const glm::vec3 apart = glm::normalize(glm::cross(ghost.goal - ghost.position + ghost.up * 1e-3f, ghost.up) + upBasis(ghost.up)[0] * 1e-3f);
        half.goal = ghost.position + apart * def.vasskraka.scatterDistance;
        ghost.goal = ghost.position - apart * def.vasskraka.scatterDistance;
        events.push_back(KrakaSplit{ghost.id, half.id, ghost.position});
        m_ghosts.push_back(half);
    }
    return true;
}

void GhostWorld::damageArea(const glm::vec3& center, float radius, float rounds, DamageKind kind, EventList& events,
                            std::uint32_t source) {
    std::vector<std::uint32_t> inside;
    for (const Ghost& ghost : m_ghosts) {
        const GhostHitSphere sphere = ghostHitSphere(ghost, m_data->types[ghost.type]);
        if (!ghostUntouchable(ghost) && glm::distance(sphere.center, center) < radius + sphere.radius) {
            inside.push_back(ghost.id);
        }
    }
    for (const std::uint32_t id : inside) {
        if (const Ghost* ghost = find(id)) {
            damage(id, rounds, kind, ghost->position, events, source);
        }
    }
}

void GhostWorld::reactToShot(const glm::vec3& origin, const glm::vec3& direction, bool dodgeable, EventList& events) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (Ghost& ghost : m_ghosts) {
        const GhostDef& def = m_data->types[ghost.type];
        const glm::vec3 to = ghost.position - origin;

        if (glm::length(to) < def.hearingRange && !ghost.perceives) {
            ghost.lastKnown = origin;
            ghost.sinceSeen = std::min(ghost.sinceSeen, def.memory * 0.5f);
        }
        const bool lyingLow = (def.behavior == GhostBehavior::Mimic &&
                               (ghost.state == GhostState::Disguised || ghost.state == GhostState::Reveal || ghost.state == GhostState::Conceal || !ghost.attached)) ||
                              (def.behavior == GhostBehavior::Vasskraka &&
                               (ghost.state == GhostState::Roost || ghost.state == GhostState::Gather || ghost.state == GhostState::Fall ||
                                ghost.state == GhostState::Reform || ghost.state == GhostState::Scatter || ghost.state == GhostState::Merge));
        if (!dodgeable || !ghost.perceives || def.dodgeChance <= 0.0f || ghost.hitstop > 0.0f || ghost.dying || lyingLow ||
            ghostAttacking(ghost, def) || ghost.caught > 0.0f) {
            continue;
        }

        const float along = glm::dot(to, direction);
        if (along <= 0.0f) {
            continue;
        }
        const glm::vec3 off = to - direction * along;
        if (glm::length(off) > def.radius + 0.6f || unit(m_rng) >= def.dodgeChance) {
            continue;
        }

        glm::vec3 side = glm::length(off) > 1e-3f ? glm::normalize(off) : glm::cross(direction, ghost.up);
        if (glm::length(side) < 1e-3f) {
            side = upBasis(ghost.up)[0];
        }
        side = glm::normalize(side + ghost.up * 0.3f);
        ghost.dodging = 0.3f;
        ghost.velocity = side * 9.0f;
        events.push_back(GhostDodged{ghost.id, ghost.position, side});
    }
}

void GhostWorld::push(const glm::vec3& center, float radius, float deltaV) {
    for (Ghost& ghost : m_ghosts) {
        if (def(ghost).windImmune) {
            continue;
        }
        const glm::vec3 d = ghost.position - center;
        const float distance = glm::length(d);
        if (distance < radius && distance > 1e-3f) {
            const float shove = deltaV * (1.0f - distance / radius) * (1.0f - def(ghost).weight);
            ghost.velocity += d / distance * shove;
            if (def(ghost).behavior == GhostBehavior::Vasskraka && !ghost.dying) {
                ghost.health = std::max(std::min(ghost.health, 1.0f), ghost.health - def(ghost).vasskraka.pushDamage * std::max(shove, 0.0f));
            }
        }
    }
}

bool GhostWorld::pose(std::uint32_t id, const Pose& pose) {
    const auto it = std::find_if(m_ghosts.begin(), m_ghosts.end(), [id](const Ghost& g) { return g.id == id; });
    if (it == m_ghosts.end()) {
        return false;
    }
    Ghost& ghost = *it;
    if (!ghost.posed || ghost.state != pose.state) {
        ghost.stateTime = 0.0f;
    }
    ghost.posed = true;
    ghost.poseLoop = pose.loop;
    ghost.state = pose.state;
    ghost.position = pose.position;
    ghost.home = pose.position;
    ghost.velocity = pose.velocity;
    ghost.surfaceNormal = pose.normal;
    ghost.attached = pose.attached;
    ghost.perceives = false;
    ghost.hitstop = 0.0f;
    ghost.dodging = 0.0f;
    if (pose.health >= 0.0f) {
        ghost.health = pose.health;
    }
    return true;
}

bool GhostWorld::remove(std::uint32_t id) { return std::erase_if(m_ghosts, [id](const Ghost& g) { return g.id == id; }) > 0; }

void GhostWorld::setOn(std::uint32_t id, std::uint8_t player) {
    for (Ghost& ghost : m_ghosts) {
        if (ghost.id == id) {
            ghost.prefers = player;
        }
    }
}

void GhostWorld::forget() {
    for (Ghost& ghost : m_ghosts) {
        ghost.perceives = false;
        ghost.sinceSeen = 1e6f;
        ghost.awayTime = 0.0f;
        switch (def(ghost).behavior) {
        case GhostBehavior::BallLightning: ghost.state = GhostState::Charge; break;
        case GhostBehavior::Necromite: break;
        case GhostBehavior::Vasskraka:
            if (ghost.state != GhostState::Roost) {
                ghost.state = GhostState::Circle;
            }
            break;
        case GhostBehavior::Mimic:
            if (ghost.state != GhostState::Disguised) {
                ghost.state = GhostState::Pause;
            }
            break;
        default: ghost.state = GhostState::Wander; break;
        }
        ghost.stateTime = 0.0f;
    }
}

}
