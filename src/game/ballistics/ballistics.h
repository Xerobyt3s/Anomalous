#pragma once

#include "engine/physics/physics_world.h"
#include "game/ammo/round.h"
#include "game/events.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace ghost::game {
struct BallisticsTuning {
    float muzzleVelocity = 430.0f;
    float dragPerMetre = 0.0012f;
    float gravity = 9.81f;
    float maxLifetime = 3.0f;
    float minRicochetSpeed = 80.0f;
    int maxRicochets = 2;
};

struct Projectile {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    glm::vec3 previousPosition{0.0f};
    glm::vec3 velocity{0.0f};
    Round round;
    float dragScale = 1.0f;
    float age = 0.0f;
    int ricochets = 0;
    bool ethereal = false;
    float dragPerMetre = 0.0f;
    float gravityScale = 1.0f;
    float fadeSpeed = 0.0f;
    bool stopped = false;
    float held = 0.0f;
    std::uint8_t owner = 255;
};

struct ShotProfile {
    float velocityScale = 1.0f;
    float dragScale = 1.0f;
    bool ethereal = false;
    float dragPerMetre = 0.0f;
    float gravityScale = 1.0f;
    float fadeSpeed = 0.0f;
    std::uint8_t owner = 255;
    glm::vec3 inheritVelocity{0.0f};
};

using RaycastFn = std::function<std::optional<engine::RayHit>(const glm::vec3& from, const glm::vec3& to)>;

using VolumeReactFn = std::function<std::optional<ElementId>(const glm::vec3& from, const glm::vec3& to,
                                                             ElementId element, glm::vec3* where)>;

using FieldFn = std::function<glm::vec3(const glm::vec3& at)>;

using ProjectileHitFn =
    std::function<std::optional<engine::RayHit>(const Projectile& projectile, const glm::vec3& from, const glm::vec3& to)>;

class Ballistics {
public:

    void fire(const glm::vec3& origin, const glm::vec3& direction, const Round& round, float velocityScale = 1.0f,
              float dragScale = 1.0f);
    void fire(const glm::vec3& origin, const glm::vec3& direction, const Round& round, const ShotProfile& profile);

    void tick(float dt, const RaycastFn& raycast, EventList& events, const VolumeReactFn& volumeReact = {},
              const RaycastFn& etherealRaycast = {}, const ProjectileHitFn& alsoHits = {});

    const std::vector<Projectile>& projectiles() const { return m_projectiles; }

    void remove(std::uint32_t id) {
        std::erase_if(m_projectiles, [id](const Projectile& p) { return p.id == id; });
    }

    void adopt(const Projectile& projectile) { m_projectiles.push_back(projectile); }

    void hold(std::uint32_t id, float seconds) {
        for (Projectile& p : m_projectiles) {
            if (p.id == id && p.stopped) {
                p.held = seconds;
            }
        }
    }
    void setFields(FieldFn gravity, FieldFn wind) {
        m_gravityAt = std::move(gravity);
        m_windAt = std::move(wind);
    }
    void setRicochet(std::vector<float> scales) { m_ricochet = std::move(scales); }
    BallisticsTuning& tuning() { return m_tuning; }
    const BallisticsTuning& tuning() const { return m_tuning; }

private:
    BallisticsTuning m_tuning;
    std::vector<Projectile> m_projectiles;
    std::uint32_t m_nextId = 1;
    std::vector<float> m_ricochet;
    FieldFn m_gravityAt;
    FieldFn m_windAt;
};

}
