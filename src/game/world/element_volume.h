#pragma once

#include "game/ammo/ammo_data.h"
#include "game/events.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace ghost::game {
struct ElementVolume {
    std::uint32_t id = 0;
    ElementId element = kPlainElement;
    glm::vec3 center{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float radius = 0.5f;
    float age = 0.0f;
    float lifetime = 1.0f;
    glm::vec3 velocity{0.0f};

    float remaining01() const { return lifetime > 0.0f ? 1.0f - age / lifetime : 0.0f; }
};

class ElementVolumes {
public:
    const ElementVolume& spawn(ElementId element, const glm::vec3& center, const glm::vec3& normal, float radius,
                               float lifetime);

    void tick(float dt, const AmmoData& data, EventList& events);

    std::optional<ElementId> reactAlong(const glm::vec3& from, const glm::vec3& to, ElementId element,
                                        const ReactionTable& reactions, glm::vec3* where = nullptr) const;

    const std::vector<ElementVolume>& all() const { return m_volumes; }

    void adjust(std::uint32_t id, const glm::vec3& drift, float extraAge);
    void remove(std::uint32_t id);

    void push(std::uint32_t id, const glm::vec3& velocity);

    void refresh(std::uint32_t id, float age);
    void removeElement(ElementId element);

    void settle(float dt, const std::function<bool(ElementId)>& settles,
                const std::function<std::optional<float>(const glm::vec3&)>& groundBelow);
    static constexpr float kRestHeight = 0.08f;
    static constexpr float kStepUp = 0.5f;

    void replace(std::vector<ElementVolume> volumes) { m_volumes = std::move(volumes); }
    void extrapolate(float dt) {
        for (ElementVolume& v : m_volumes) {
            v.age += dt;
            v.center += v.velocity * dt;
        }
    }

private:
    std::vector<ElementVolume> m_volumes;
    std::uint32_t m_nextId = 1;
};

std::optional<float> segmentSphere(const glm::vec3& from, const glm::vec3& to, const glm::vec3& center, float radius);

}
