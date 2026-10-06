#include "game/world/element_volume.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
void ElementVolumes::adjust(std::uint32_t id, const glm::vec3& drift, float extraAge) {
    for (ElementVolume& v : m_volumes) {
        if (v.id == id) {
            v.center += drift;
            v.age += extraAge;
        }
    }
}

void ElementVolumes::push(std::uint32_t id, const glm::vec3& velocity) {
    for (ElementVolume& v : m_volumes) {
        if (v.id == id) {
            v.velocity = velocity;
        }
    }
}

void ElementVolumes::refresh(std::uint32_t id, float age) {
    for (ElementVolume& v : m_volumes) {
        if (v.id == id) {
            v.age = std::min(v.age, age);
        }
    }
}

void ElementVolumes::remove(std::uint32_t id) {
    std::erase_if(m_volumes, [id](const ElementVolume& v) { return v.id == id; });
}

void ElementVolumes::removeElement(ElementId element) {
    std::erase_if(m_volumes, [element](const ElementVolume& v) { return v.element == element; });
}

std::optional<float> segmentSphere(const glm::vec3& from, const glm::vec3& to, const glm::vec3& center, float radius) {
    const glm::vec3 d = to - from;
    const glm::vec3 m = from - center;
    const float c = glm::dot(m, m) - radius * radius;
    if (c <= 0.0f) {
        return 0.0f;
    }
    const float a = glm::dot(d, d);
    const float b = glm::dot(m, d);
    if (a < 1e-12f || b > 0.0f) {
        return std::nullopt;
    }
    const float disc = b * b - a * c;
    if (disc < 0.0f) {
        return std::nullopt;
    }
    const float t = (-b - std::sqrt(disc)) / a;
    return t <= 1.0f ? std::optional<float>(t) : std::nullopt;
}

const ElementVolume& ElementVolumes::spawn(ElementId element, const glm::vec3& center, const glm::vec3& normal,
                                           float radius, float lifetime) {
    ElementVolume v;
    v.id = m_nextId++;
    v.element = element;
    v.center = center;
    v.normal = normal;
    v.radius = radius;
    v.lifetime = lifetime;
    m_volumes.push_back(v);
    return m_volumes.back();
}

void ElementVolumes::tick(float dt, const AmmoData& data, EventList& events) {
    for (ElementVolume& v : m_volumes) {
        v.age += dt;
        v.center += v.velocity * dt;
    }

    for (std::size_t i = 0; i < m_volumes.size(); ++i) {
        for (std::size_t j = i + 1; j < m_volumes.size(); ++j) {
            ElementVolume& a = m_volumes[i];
            ElementVolume& b = m_volumes[j];
            if (a.age >= a.lifetime || b.age >= b.lifetime) {
                continue;
            }
            if (glm::distance(a.center, b.center) > a.radius + b.radius) {
                continue;
            }
            const auto result = data.reactions.react(a.element, b.element);
            if (!result) {
                continue;
            }
            ElementVolume& keep = (a.lifetime - a.age) >= (b.lifetime - b.age) ? a : b;
            ElementVolume& consumed = (&keep == &a) ? b : a;
            const ElementDef& def = data.elements[*result];
            keep.element = *result;
            keep.radius = std::max(keep.radius, def.volumeRadius);
            keep.age = 0.0f;
            keep.lifetime = std::max(def.volumeLifetime, 0.5f);
            consumed.age = consumed.lifetime;
            events.push_back(VolumeReacted{keep.center, keep.element});
        }
    }

    std::erase_if(m_volumes, [](const ElementVolume& v) { return v.age >= v.lifetime; });
}

void ElementVolumes::settle(float dt, const std::function<bool(ElementId)>& settles,
                            const std::function<std::optional<float>(const glm::vec3&)>& groundBelow) {
    settle(dt, settles, [&](const glm::vec3& at, const glm::vec3&) { return groundBelow(at); },
           [](const glm::vec3&) { return glm::vec3(0.0f, 1.0f, 0.0f); });
}

void ElementVolumes::settle(float dt, const std::function<bool(ElementId)>& settles,
                            const std::function<std::optional<float>(const glm::vec3&, const glm::vec3&)>& groundBelow,
                            const std::function<glm::vec3(const glm::vec3&)>& upAt) {
    for (ElementVolume& v : m_volumes) {
        if (!settles(v.element)) {
            continue;
        }
        const glm::vec3 up = upAt(v.center);
        v.normal = up;
        const auto ground = groundBelow(v.center, up);
        const float rest = ground ? *ground + kRestHeight : -1e9f;
        const float height = glm::dot(v.center, up);
        float rise = glm::dot(v.velocity, up);
        const glm::vec3 lateral = v.velocity - up * rise;
        if (height <= rest + 1e-3f) {
            v.center += up * (rest - height);
            rise = 0.0f;
        } else {
            rise = std::max(rise - 9.81f * dt, -30.0f);
            if (height + rise * dt < rest) {
                rise = (rest - height) / std::max(dt, 1e-4f);
            }
        }
        v.velocity = lateral + up * rise;
    }
}

std::optional<ElementId> ElementVolumes::reactAlong(const glm::vec3& from, const glm::vec3& to, ElementId element,
                                                    const ReactionTable& reactions, glm::vec3* where) const {
    float best = 2.0f;
    std::optional<ElementId> result;
    for (const ElementVolume& v : m_volumes) {
        const auto reacted = reactions.react(element, v.element);
        if (!reacted) {
            continue;
        }
        if (const auto t = segmentSphere(from, to, v.center, v.radius); t && *t < best) {
            best = *t;
            result = reacted;
        }
    }
    if (result && where) {
        *where = from + (to - from) * best;
    }
    return result;
}

}
