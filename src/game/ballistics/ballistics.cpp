#include "game/ballistics/ballistics.h"

#include "game/ballistics/surface.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
void Ballistics::fire(const glm::vec3& origin, const glm::vec3& direction, const Round& round, float velocityScale,
                      float dragScale) {
    ShotProfile profile;
    profile.velocityScale = velocityScale;
    profile.dragScale = dragScale;
    fire(origin, direction, round, profile);
}

void Ballistics::fire(const glm::vec3& origin, const glm::vec3& direction, const Round& round,
                      const ShotProfile& profile) {
    Projectile p;
    p.id = m_nextId++;
    p.position = origin;
    p.previousPosition = origin;
    p.velocity = glm::normalize(direction) * m_tuning.muzzleVelocity * profile.velocityScale + profile.inheritVelocity;
    p.dragScale = profile.dragScale;
    p.ethereal = profile.ethereal;
    p.dragPerMetre = profile.dragPerMetre;
    p.gravityScale = profile.gravityScale;
    p.fadeSpeed = profile.fadeSpeed;
    p.owner = profile.owner;
    p.round = round;
    m_projectiles.push_back(p);
}

void Ballistics::tick(float dt, const RaycastFn& raycast, EventList& events, const VolumeReactFn& volumeReact,
                      const RaycastFn& etherealRaycast, const ProjectileHitFn& alsoHits) {
    const glm::vec3 gravity{0.0f, -m_tuning.gravity, 0.0f};

    for (Projectile& p : m_projectiles) {
        p.previousPosition = p.position;
        if (p.stopped) {
            p.held -= dt;
            if (p.held <= 0.0f) {
                p.age = m_tuning.maxLifetime + 1.0f;
            }
            continue;
        }
        p.age += dt;

        const float k = p.dragPerMetre > 0.0f ? p.dragPerMetre : m_tuning.dragPerMetre * p.dragScale;
        const glm::vec3 air = m_windAt ? m_windAt(p.position) : glm::vec3(0.0f);
        const glm::vec3 relative = p.velocity - air;
        p.velocity = air + relative / (1.0f + k * glm::length(relative) * dt);
        const glm::vec3 pull = m_gravityAt ? m_gravityAt(p.position) * (m_tuning.gravity / 9.81f) : gravity;
        p.velocity += pull * (p.gravityScale * dt);
        const glm::vec3 target = p.position + p.velocity * dt;

        std::optional<engine::RayHit> hit = !p.ethereal ? raycast(p.position, target)
                                                        : (etherealRaycast ? etherealRaycast(p.position, target) : std::nullopt);
        if (alsoHits) {
            if (const auto other = alsoHits(p, p.position, target); other && (!hit || other->fraction < hit->fraction)) {
                hit = other;
            }
        }
        const glm::vec3 reachEnd = hit ? hit->point : target;

        if (volumeReact) {
            glm::vec3 where{0.0f};
            if (const auto changed = volumeReact(p.position, reachEnd, p.round.element, &where)) {
                p.round.element = *changed;
                events.push_back(ProjectileTransformed{p.id, where, *changed});
            }
        }

        if (!hit) {
            p.position = target;
            if (p.fadeSpeed > 0.0f && glm::length(p.velocity) < p.fadeSpeed) {
                events.push_back(ProjectileFaded{p.position, p.round.element, p.id, p.owner});
                p.age = m_tuning.maxLifetime + 1.0f;
            }
            continue;
        }

        const float speed = glm::length(p.velocity);
        const glm::vec3 dir = p.velocity / speed;
        glm::vec3 normal = hit->normal;
        if (glm::dot(dir, normal) > 0.0f) {
            normal = -normal;
        }
        const auto surface = static_cast<Surface>(hit->userData);
        const SurfaceResponse response = surfaceResponse(surface);

        const float grazingDeg = glm::degrees(std::asin(std::clamp(-glm::dot(dir, normal), 0.0f, 1.0f)));
        const bool ricochet = grazingDeg <= response.ricochetMaxAngleDeg && speed >= m_tuning.minRicochetSpeed &&
                              p.ricochets < m_tuning.maxRicochets;

        ProjectileImpact impact;
        impact.point = hit->point;
        impact.normal = normal;
        impact.direction = dir;
        impact.surface = surface;
        impact.speed = speed;
        impact.ricochet = ricochet;
        impact.element = p.round.element;
        impact.projectile = p.id;
        impact.shooter = p.owner;
        impact.body = hit->body;
        impact.dynamicBody = hit->dynamic;
        events.push_back(impact);

        if (ricochet) {
            p.velocity = glm::reflect(p.velocity, normal) * response.ricochetSpeedRetain;
            p.position = hit->point + normal * 0.001f;
            ++p.ricochets;
        } else {
            p.position = hit->point;
            if (surface == Surface::Ghost) {
                p.stopped = true;
            } else {
                p.age = m_tuning.maxLifetime + 1.0f;
            }
        }
    }

    std::erase_if(m_projectiles, [&](const Projectile& p) { return p.age > m_tuning.maxLifetime; });
}

}
