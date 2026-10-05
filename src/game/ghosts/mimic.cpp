#include "game/ghosts/mimic.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr float kGravity = 9.81f;
constexpr float kPickupReach = 1.95f;
constexpr float kViewCone = 0.5f;

void enter(Ghost& ghost, GhostState state) {
    ghost.state = state;
    ghost.stateTime = 0.0f;
}

glm::vec3 chestOf(const GhostQuarry& quarry) { return glm::mix(quarry.feet, quarry.eye, 0.6f); }

glm::vec3 flat(const glm::vec3& v) { return {v.x, 0.0f, v.z}; }

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-4f ? v / length : fallback;
}

bool blocked(const GhostContext& context, const glm::vec3& from, const glm::vec3& to) {
    return context.blocked && context.blocked(from, to);
}

bool watching(const GhostContext& context, const GhostQuarry& player, const glm::vec3& at) {
    const glm::vec3 to = at - player.eye;
    const float distance = glm::length(to);
    if (distance < 0.3f) {
        return true;
    }
    return glm::dot(to / distance, player.viewDirection) > kViewCone && !blocked(context, player.eye, at);
}

void stick(Ghost& ghost, const GhostSurfaceHit& hit, float radius) {
    ghost.position = hit.point + hit.normal * radius;
    ghost.surfaceNormal = hit.normal;
    ghost.attached = true;
}

glm::vec3 stride(const glm::vec3& moved, float speed, float dt) {
    const glm::vec3 v = moved / std::max(dt, 1e-4f);
    const float length = glm::length(v);
    return length > speed ? v * (speed / length) : v;
}

void crawl(Ghost& ghost, const MimicParams& p, const GhostContext& context, const glm::vec3& goal, float speed, float dt) {
    const glm::vec3 start = ghost.position;
    const glm::vec3 n = ghost.surfaceNormal;
    const float r = p.bodyRadius;
    const glm::vec3 to = goal - ghost.position;
    const glm::vec3 along = to - n * glm::dot(to, n);
    const float length = glm::length(along);
    if (length < 0.05f) {
        ghost.velocity = glm::vec3(0.0f);
        return;
    }

    const glm::vec3 aim = along / length;
    const glm::vec3 going = ghost.velocity - n * glm::dot(ghost.velocity, n);
    const float pace = glm::length(going);
    glm::vec3 m = aim;
    if (pace > 0.3f && length > 0.5f) {
        const glm::vec3 heading = going / pace;
        const float angle = std::acos(glm::clamp(glm::dot(heading, aim), -1.0f, 1.0f));
        const float turn = p.turnRate * dt;
        if (angle > turn) {
            const float way = glm::dot(glm::cross(heading, aim), n) >= 0.0f ? 1.0f : -1.0f;
            m = glm::normalize(heading * std::cos(turn) + glm::cross(n, heading) * (std::sin(turn) * way));
        }
    }
    const float wanted = speed * glm::mix(0.35f, 1.0f, std::max(glm::dot(m, aim), 0.0f));
    const float step = std::min(std::min(wanted, pace + p.acceleration * dt) * dt, length);

    if (const auto hit = castSurface(context, ghost.position, ghost.position + m * (step + r))) {
        if (glm::dot(hit->normal, n) < 0.9f) {
            if (hit->normal.y < 0.5f && n.y > 0.5f) {
                ghost.climb = 1;
            }
            stick(ghost, *hit, r);
            ghost.velocity = stride(ghost.position - start, speed, dt);
            return;
        }
    }
    ghost.position += m * step;
    if (const auto hit = castSurface(context, ghost.position, ghost.position - n * (r + 0.35f))) {
        stick(ghost, *hit, r);
    } else {
        const glm::vec3 probe = ghost.position - n * (r + 0.3f);
        if (const auto under = castSurface(context, probe, probe - m * (r + 0.6f))) {
            if (under->normal.y < 0.5f && n.y > 0.5f) {
                ghost.climb = -1;
            }
            stick(ghost, *under, r);
        } else {
            ghost.attached = false;
            ghost.velocity = m * speed;
            return;
        }
    }
    ghost.velocity = stride(ghost.position - start, speed, dt);
}

void fly(Ghost& ghost, const MimicParams& p, const GhostContext& context, float dt) {
    ghost.velocity.y -= kGravity * dt;
    const glm::vec3 next = ghost.position + ghost.velocity * dt;
    const float speed = glm::length(ghost.velocity);
    const glm::vec3 ahead = speed > 1e-4f ? ghost.velocity / speed : -kUp;
    if (const auto hit = castSurface(context, ghost.position, next + ahead * p.bodyRadius)) {
        stick(ghost, *hit, p.bodyRadius);
        ghost.climb = 1;
        ghost.velocity = glm::vec3(0.0f);
        return;
    }
    ghost.position = next;
}

std::optional<GhostSurfaceHit> wallNear(const GhostContext& context, const glm::vec3& around, float reach) {
    std::optional<GhostSurfaceHit> best;
    float bestDistance = 1e9f;
    const glm::vec3 from = around + kUp * 1.0f;
    for (int k = 0; k < 8; ++k) {
        const float angle = static_cast<float>(k) * 0.7853982f;
        const glm::vec3 direction{std::cos(angle), 0.0f, std::sin(angle)};
        if (const auto hit = castSurface(context, from, from + direction * reach)) {
            const float distance = glm::distance(hit->point, from);
            if (std::abs(hit->normal.y) < 0.3f && distance < bestDistance) {
                best = hit;
                bestDistance = distance;
            }
        }
    }
    return best;
}

glm::vec3 onFloor(const GhostContext& context, const glm::vec3& at, float radius) {
    if (const auto hit = castSurface(context, at + kUp * 1.5f, at - kUp * 6.0f)) {
        return hit->point + kUp * radius;
    }
    return at;
}

float dash(const MimicParams& p, GhostAccess& access) { return glm::mix(p.burstMin, p.burstMax, access.random01()); }
float rest(const MimicParams& p, GhostAccess& access) { return glm::mix(p.pauseMin, p.pauseMax, access.random01()); }

glm::vec3 pickSpot(const Ghost& ghost, const MimicParams& p, const GhostContext& context, const GhostQuarry& prey,
                   GhostAccess& access) {
    constexpr int kSpots = 16;
    const glm::vec3 view = normalizeOr(flat(prey.viewDirection), glm::vec3(0.0f, 0.0f, -1.0f));
    const float turn = access.random01() * glm::two_pi<float>();
    glm::vec3 best = ghost.position;
    float bestScore = -1e9f;
    for (int k = 0; k < kSpots; ++k) {
        const float angle = turn + static_cast<float>(k) * glm::two_pi<float>() / static_cast<float>(kSpots);
        const glm::vec3 out{std::cos(angle), 0.0f, std::sin(angle)};
        const float radius = glm::mix(p.spotMin, p.spotMax, access.random01());
        glm::vec3 spot = onFloor(context, prey.feet + out * radius, p.bodyRadius);
        if (blocked(context, spot, spot + kUp * 0.5f)) {
            continue;
        }
        float score = 0.0f;
        const bool hidden = blocked(context, prey.eye, spot + kUp * 0.2f);
        score += hidden ? 3.0f : 0.0f;
        score += 2.0f * std::max(0.0f, -glm::dot(out, view));

        if (const auto wall = wallNear(context, spot - kUp * p.bodyRadius, 1.2f)) {
            if (glm::dot(wall->normal, flat(prey.feet - wall->point)) < 0.0f) {
                score += 2.5f;
                spot = glm::vec3(wall->point.x, spot.y, wall->point.z) + wall->normal * 0.05f;
            }
        }
        const float distance = glm::distance(ghost.position, spot);
        score -= 0.25f * distance;
        score -= distance < 1.0f ? 1.0f : 0.0f;
        if (watching(context, prey, glm::mix(ghost.position, spot, 0.5f))) {
            score -= 2.0f;
        }
        if (score > bestScore) {
            bestScore = score;
            best = spot;
        }
    }
    return best;
}

glm::vec3 pickRefuge(const Ghost& ghost, const MimicParams& p, const GhostContext& context, GhostAccess& access) {
    const glm::vec3 from = ghost.lastKnown + kUp * 1.6f;
    const glm::vec3 away = normalizeOr(flat(ghost.position - ghost.lastKnown), glm::vec3(1.0f, 0.0f, 0.0f));
    glm::vec3 best = onFloor(context, ghost.position + away * p.fleeMin, p.bodyRadius);
    float bestScore = -1e9f;
    for (int k = 0; k < 12; ++k) {
        const float angle = static_cast<float>(k) * glm::two_pi<float>() / 12.0f + access.random01() * 0.4f;
        const glm::vec3 out{std::cos(angle), 0.0f, std::sin(angle)};
        const glm::vec3 spot = onFloor(context, ghost.position + out * glm::mix(p.fleeMin, p.fleeMax, access.random01()), p.bodyRadius);
        if (blocked(context, spot, spot + kUp * 0.5f)) {
            continue;
        }
        const float score = (blocked(context, from, spot) ? 4.0f : 0.0f) + 2.0f * glm::dot(out, away) +
                            0.1f * glm::distance(spot, ghost.lastKnown);
        if (score > bestScore) {
            bestScore = score;
            best = spot;
        }
    }
    return best;
}

void leapAt(Ghost& ghost, const MimicParams& p, const glm::vec3& target) {
    const glm::vec3 d = target - ghost.position;
    const glm::vec3 across = flat(d);

    const float time = std::max({glm::length(across) / p.leapSpeed, std::sqrt(2.0f * std::max(-d.y, 0.0f) / kGravity), 0.2f});
    ghost.velocity = across / time + kUp * (d.y / time + 0.5f * kGravity * time);
    ghost.attached = false;
    ghost.leapHit = false;
    enter(ghost, GhostState::Leap);
}

void startHunt(Ghost& ghost, const MimicParams& p, const GhostContext& context, const GhostQuarry* prey, GhostAccess& access) {
    enter(ghost, GhostState::Hunt);
    ghost.timer = dash(p, access);
    ghost.goal = prey ? pickSpot(ghost, p, context, *prey, access) : ghost.position;
}

}

void tickMimic(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, GhostAccess& access) {
    const MimicParams& p = def.mimic;

    bool known = false;
    if (ghost.target >= 0 && ghost.target < static_cast<int>(context.players.size())) {
        const GhostQuarry& last = context.players[static_cast<std::size_t>(ghost.target)];
        known = ghost.perceives || (!last.hidden && (ghost.sinceSeen < def.memory || glm::distance(ghost.position, last.feet) < p.senseRange));
    }
    const GhostQuarry* prey = known ? &context.players[static_cast<std::size_t>(ghost.target)] : nullptr;
    ghost.cooldown = std::max(0.0f, ghost.cooldown - dt);
    const bool stalking = ghost.state == GhostState::Hunt || ghost.state == GhostState::Pause;

    if (ghost.dodging > 0.0f && ghost.attached && (stalking || ghost.state == GhostState::Flee)) {
        const glm::vec3 side = normalizeOr(ghost.velocity - ghost.surfaceNormal * glm::dot(ghost.velocity, ghost.surfaceNormal),
                                           glm::cross(ghost.surfaceNormal, glm::vec3(0.3f, 0.2f, 0.9f)));
        crawl(ghost, p, context, ghost.position + side * 2.0f, 9.0f, dt);
        return;
    }

    if (stalking && !prey && ghost.attached) {
        enter(ghost, GhostState::Flee);
        ghost.goal = pickRefuge(ghost, p, context, access);
        if (ghost.surfaceNormal.y < 0.5f) {
            ghost.attached = false;
            ghost.velocity = ghost.surfaceNormal * 1.5f;
        }
    }

    if (stalking && prey && ghost.attached) {
        ghost.awayTime += dt;
        const glm::vec3 chest = chestOf(*prey);
        const float distance = glm::distance(ghost.position, chest);
        const bool climbing = ghost.surfaceNormal.y < 0.5f;

        if (ghost.cooldown <= 0.0f && distance < (climbing ? std::min(p.thrashRange, 1.0f) : p.thrashRange)) {
            enter(ghost, GhostState::Windup);
            ghost.velocity = glm::vec3(0.0f);
            access.events.push_back(MimicThrash{ghost.id, ghost.position, chest, p.thrashWindup});
            return;
        }

        const float across = glm::length(flat(ghost.position - prey->feet));
        const bool above = ghost.position.y > prey->feet.y + 1.3f;
        const bool inRange = above ? across < p.leapMax + 2.0f : (!climbing && across > p.leapMin && across < p.leapMax);
        if (inRange && !blocked(context, ghost.position + ghost.surfaceNormal * 0.1f, chest)) {
            const bool watched = watching(context, *prey, ghost.position);
            const float rate = !watched ? 4.0f : (ghost.awayTime > p.patience ? 3.0f : (above ? 0.8f : 0.0f));
            if (access.random01() < rate * dt) {
                leapAt(ghost, p, chest);
                ghost.awayTime = 0.0f;
                return;
            }
        }
    }

    switch (ghost.state) {
    case GhostState::Disguised: {
        ghost.velocity = glm::vec3(0.0f);
        if (const auto ground = castSurface(context, ghost.position, ghost.position - kUp * (p.bodyRadius + 0.05f))) {
            stick(ghost, *ground, p.bodyRadius);
        } else {
            ghost.position.y -= 2.5f * dt;
        }
        bool lingering = false;
        bool grabbed = false;
        for (const GhostQuarry& player : context.players) {
            const float distance = glm::distance(ghost.position, player.feet + kUp * 0.3f);
            lingering = lingering || (!player.hidden && distance < p.revealNear);
            grabbed = grabbed || (player.interacting && distance < kPickupReach);
        }
        if (lingering) {
            ghost.timer += dt;
        }
        ghost.awayTime = grabbed ? ghost.awayTime + dt : 0.0f;
        if (ghost.timer >= p.revealLinger || ghost.awayTime >= p.revealGrab) {
            enter(ghost, GhostState::Reveal);
            access.events.push_back(MimicRevealed{ghost.id, ghost.position, ghost.disguise});
        }
        break;
    }
    case GhostState::Reveal:
        ghost.velocity = glm::vec3(0.0f);

        if (ghost.stateTime <= dt * 1.5f) {
            ghost.struck = 0;
        }
        if (ghost.stateTime > p.revealTime * 0.2f) {
            for (std::size_t i = 0; i < context.players.size() && i < 8; ++i) {
                const GhostQuarry& player = context.players[i];
                const auto bit = static_cast<std::uint8_t>(1u << i);
                if ((ghost.struck & bit) == 0 && distanceToSegment(ghost.position, player.feet, player.eye) < p.revealRadius) {
                    ghost.struck = static_cast<std::uint8_t>(ghost.struck | bit);
                    const glm::vec3 away = normalizeOr(flat(chestOf(player) - ghost.position), glm::vec3(1.0f, 0.0f, 0.0f));
                    access.events.push_back(PlayerDamaged{p.revealDamage, ghost.position, static_cast<int>(i),
                                                          (away + kUp * 0.35f) * p.revealShove});
                }
            }
        }
        if (ghost.stateTime >= p.revealTime) {
            ghost.awayTime = 0.0f;
            startHunt(ghost, p, context, prey, access);
        }
        break;
    case GhostState::Windup:
        if (!ghost.attached) {
            fly(ghost, p, context, dt);
        }
        if (ghost.stateTime >= p.thrashWindup) {
            for (std::size_t i = 0; i < context.players.size(); ++i) {
                const GhostQuarry& player = context.players[i];
                if (distanceToSegment(ghost.position, player.feet, player.eye) < p.thrashReach) {
                    const glm::vec3 away = normalizeOr(flat(chestOf(player) - ghost.position), glm::vec3(1.0f, 0.0f, 0.0f));
                    access.events.push_back(PlayerDamaged{p.thrashDamage, ghost.position, static_cast<int>(i),
                                                          (away + kUp * 0.35f) * p.thrashShove});
                }
            }
            ghost.cooldown = p.thrashCooldown;
            ghost.awayTime = 0.0f;
            enter(ghost, GhostState::Pause);
            ghost.timer = rest(p, access) * 0.5f;
        }
        break;
    case GhostState::Leap:

        if (!ghost.leapHit) {
            for (std::size_t i = 0; i < context.players.size(); ++i) {
                const GhostQuarry& player = context.players[i];
                if (distanceToSegment(ghost.position, player.feet, player.eye) < p.leapHitRadius) {
                    const glm::vec3 along = normalizeOr(flat(ghost.velocity), glm::vec3(1.0f, 0.0f, 0.0f));
                    access.events.push_back(PlayerDamaged{p.leapDamage, ghost.position, static_cast<int>(i),
                                                          (along + kUp * 0.3f) * p.leapShove});
                    ghost.leapHit = true;
                    break;
                }
            }
        }
        fly(ghost, p, context, dt);
        if (ghost.attached) {
            enter(ghost, GhostState::Pause);
            ghost.timer = rest(p, access) * 0.4f;
        }
        break;
    case GhostState::Flinch:
        if (!ghost.attached) {
            fly(ghost, p, context, dt);
        }
        if (ghost.stateTime >= p.flinchTime) {
            startHunt(ghost, p, context, prey, access);
        }
        break;
    case GhostState::Hunt: {
        if (!ghost.attached) {
            fly(ghost, p, context, dt);
            break;
        }
        if (!prey) {
            break;
        }
        ghost.timer -= dt;
        const glm::vec3 offset = ghost.position - prey->feet;
        const float across = glm::length(flat(offset));
        const bool high = ghost.position.y > prey->feet.y + p.climbHeight * 0.8f;
        const float far = p.spotMax + 3.0f;
        if (ghost.surfaceNormal.y < 0.5f) {
            if (across > far || (high && ghost.awayTime > p.patience && across > p.leapMax + 2.0f)) {
                ghost.attached = false;
                ghost.velocity = ghost.surfaceNormal * 1.5f;
                break;
            }
            if (ghost.climb < 0) {
                crawl(ghost, p, context, ghost.position - kUp * 2.0f, p.burstSpeed, dt);
                break;
            }

            if (!high) {
                crawl(ghost, p, context, ghost.position + kUp * 2.0f, p.burstSpeed, dt);
                break;
            }
            const bool facingThem = glm::dot(ghost.surfaceNormal, flat(prey->feet - ghost.position)) > 0.0f;
            if (!facingThem) {
                crawl(ghost, p, context, ghost.position + kUp * 2.0f + normalizeOr(flat(-offset), kUp) * 0.5f, p.burstSpeed, dt);
                break;
            }

            if (across < p.dropRange) {
                leapAt(ghost, p, chestOf(*prey));
                ghost.awayTime = 0.0f;
                break;
            }
            crawl(ghost, p, context, prey->feet + kUp * p.climbHeight, p.burstSpeed, dt);
            break;
        }
        if (ghost.position.y > prey->feet.y + 1.3f) {
            if (across > far || blocked(context, ghost.position + kUp * 0.1f, chestOf(*prey))) {
                crawl(ghost, p, context, prey->feet, p.burstSpeed, dt);
            } else {
                ghost.velocity = glm::vec3(0.0f);
            }
            break;
        }

        const bool committed = ghost.awayTime > p.patience;
        const bool arrived = glm::distance(ghost.position, ghost.goal) < 0.4f;
        if (!committed && (ghost.timer <= 0.0f || arrived)) {
            if (!watching(context, *prey, ghost.position)) {
                enter(ghost, GhostState::Pause);
                ghost.timer = rest(p, access);
                break;
            }
            ghost.timer = dash(p, access);
            if (arrived) {
                ghost.goal = pickSpot(ghost, p, context, *prey, access);
            }
        }
        glm::vec3 waypoint = committed ? prey->feet : ghost.goal;
        if (!committed) {
            const glm::vec3 a = flat(ghost.position);
            const glm::vec3 b = flat(waypoint);
            const glm::vec3 c = flat(prey->feet);
            const glm::vec3 ab = b - a;
            const float t = glm::clamp(glm::dot(c - a, ab) / std::max(glm::dot(ab, ab), 1e-4f), 0.0f, 1.0f);
            const glm::vec3 nearest = a + ab * t;
            if (t > 0.05f && t < 0.95f && glm::distance(nearest, c) < 2.5f) {
                const glm::vec3 wide = normalizeOr(nearest - c, glm::cross(normalizeOr(ab, glm::vec3(1.0f, 0.0f, 0.0f)), kUp));
                waypoint = glm::vec3(c.x, ghost.position.y, c.z) + wide * 3.2f;
            }
        }

        const glm::vec3 fromEye = ghost.position - prey->eye;
        if (glm::dot(normalizeOr(fromEye, kUp), prey->viewDirection) > 0.97f && !blocked(context, prey->eye, ghost.position)) {
            const glm::vec3 side = normalizeOr(glm::cross(flat(waypoint - ghost.position), kUp), glm::vec3(1.0f, 0.0f, 0.0f));
            waypoint += side * (std::sin(ghost.age * 9.0f + ghost.seed) * 1.6f);
        }
        crawl(ghost, p, context, glm::vec3(waypoint.x, ghost.position.y, waypoint.z), p.burstSpeed, dt);
        break;
    }
    case GhostState::Pause:
        if (!ghost.attached) {
            fly(ghost, p, context, dt);
            break;
        }

        if (glm::length(ghost.velocity) > 0.4f) {
            const float pace = glm::length(ghost.velocity) * std::exp(-7.0f * dt);
            crawl(ghost, p, context, ghost.position + ghost.velocity / glm::length(ghost.velocity), pace, dt);
        } else {
            ghost.velocity = glm::vec3(0.0f);
        }
        ghost.timer -= dt;
        if (!prey) {
            break;
        }

        if (ghost.timer <= 0.0f || (ghost.stateTime > 0.25f && ghost.surfaceNormal.y > 0.5f && watching(context, *prey, ghost.position))) {
            startHunt(ghost, p, context, prey, access);
        }
        break;
    case GhostState::Flee: {
        if (!ghost.attached) {
            fly(ghost, p, context, dt);
            break;
        }
        if (ghost.perceives) {
            ghost.awayTime = 0.0f;
            startHunt(ghost, p, context, prey, access);
            break;
        }
        if (ghost.surfaceNormal.y < 0.5f) {
            ghost.attached = false;
            ghost.velocity = ghost.surfaceNormal * 1.5f;
            break;
        }
        const bool there = glm::distance(flat(ghost.position), flat(ghost.goal)) < 0.5f;
        if (there || ghost.stateTime > 5.0f) {
            enter(ghost, GhostState::Conceal);
            ghost.velocity = glm::vec3(0.0f);
            ghost.disguise = pickDisguise(def, access.random01(), access.random01());
            access.events.push_back(MimicConcealed{ghost.id, ghost.position});
            break;
        }
        crawl(ghost, p, context, glm::vec3(ghost.goal.x, ghost.position.y, ghost.goal.z), p.burstSpeed, dt);
        break;
    }
    case GhostState::Conceal:
        ghost.velocity = glm::vec3(0.0f);
        if (ghost.stateTime >= p.concealTime) {
            enter(ghost, GhostState::Disguised);
            ghost.timer = 0.0f;
            ghost.awayTime = 0.0f;
            ghost.sinceSeen = 1e6f;
        }
        break;
    default:
        enter(ghost, GhostState::Pause);
        ghost.timer = rest(p, access);
        break;
    }
}

}
