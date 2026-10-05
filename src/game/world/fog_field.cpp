#include "game/world/fog_field.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr float kExtinction = 1.2f;
constexpr int kSightSamples = 32;
constexpr float kSpill = 0.2f;

}

float distanceToSegment(const glm::vec3& point, const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 d = to - from;
    const float length2 = glm::dot(d, d);
    if (length2 < 1e-8f) {
        return glm::distance(point, from);
    }
    const float t = std::clamp(glm::dot(point - from, d) / length2, 0.0f, 1.0f);
    return glm::distance(point, from + d * t);
}

glm::vec3 fogDirection(std::size_t i) {
    static const std::array<glm::vec3, kFogDirections> kDirections = [] {
        std::array<glm::vec3, kFogDirections> d{};
        d[0] = {1.0f, 0.0f, 0.0f};
        d[1] = {-1.0f, 0.0f, 0.0f};
        d[2] = {0.0f, 1.0f, 0.0f};
        d[3] = {0.0f, -1.0f, 0.0f};
        d[4] = {0.0f, 0.0f, 1.0f};
        d[5] = {0.0f, 0.0f, -1.0f};
        for (int k = 0; k < 8; ++k) {
            d[static_cast<std::size_t>(6 + k)] =
                glm::normalize(glm::vec3((k & 1) ? 1.0f : -1.0f, (k & 2) ? 1.0f : -1.0f, (k & 4) ? 1.0f : -1.0f));
        }
        return d;
    }();
    return kDirections[i];
}

FogReach measureFogReach(const glm::vec3& center, float radius, float height,
                         const std::function<std::optional<float>(const glm::vec3&, const glm::vec3&)>& hit) {
    FogReach reach = filledReach(kFogUnlimited);
    for (std::size_t i = 0; i < kFogDirections; ++i) {
        const glm::vec3 d = fogDirection(i);

        const glm::vec3 scaled{d.x / std::max(radius, 0.01f), d.y / std::max(height, 0.01f), d.z / std::max(radius, 0.01f)};
        const float length = 1.3f / std::max(glm::length(scaled), 1e-4f);
        if (const auto fraction = hit(center, center + d * length)) {
            reach[i] = std::clamp(*fraction, 0.0f, 1.0f) * length + kSpill;
        }
    }
    return reach;
}

float fogReachToward(const FogReach& reach, const glm::vec3& direction) {
    float weights = 0.0f;
    float inverse = 0.0f;
    for (std::size_t i = 0; i < kFogDirections; ++i) {
        const float a = std::max(glm::dot(direction, fogDirection(i)), 0.0f);
        const float a2 = a * a;
        const float w = a2 * a2 * a2 * a2;
        weights += w;
        inverse += w / std::max(reach[i], 1e-3f);
    }
    return weights > 1e-6f ? weights / std::max(inverse, 1e-6f) : kFogUnlimited;
}

float fogCloudDensity(const FogCloud& cloud, const glm::vec3& point) {
    if (cloud.lifetime <= 0.0f || cloud.radius <= 0.0f || cloud.height <= 0.0f) {
        return 0.0f;
    }

    const float grow = glm::smoothstep(0.0f, FogField::kGrowTime, cloud.age);
    const float size = 0.3f + 0.7f * grow;
    const float fade = std::clamp((cloud.lifetime - cloud.age) / FogField::kFadeTime, 0.0f, 1.0f);
    const float presence = std::min(1.0f, cloud.age / 0.25f) * fade;
    if (presence <= 0.0f) {
        return 0.0f;
    }
    const glm::vec3 local = point - cloud.center;

    const float rx = glm::length(glm::vec2(local.x, local.z)) / (cloud.radius * size);
    const float ry = std::abs(local.y) / (cloud.height * size);
    const float e = std::sqrt(rx * rx + ry * ry);
    float density = (1.0f - glm::smoothstep(0.55f, 1.0f, e)) * presence;

    const float distance = glm::length(local);
    if (density > 0.0f && distance > 1e-4f) {
        const float reach = fogReachToward(cloud.reach, local / distance);
        density *= 1.0f - glm::smoothstep(reach - kSpill, reach, distance);
    }
    return density;
}

float fogHoleClearing(const FogHole& hole, const glm::vec3& point) {
    if (hole.life <= 0.0f || hole.age >= hole.life) {
        return 1.0f;
    }

    const float open = std::sqrt(1.0f - hole.age / hole.life);
    const float reach = hole.radius * open;
    return glm::smoothstep(reach * 0.4f, reach, distanceToSegment(point, hole.from, hole.to));
}

void FogField::beginSync() { m_seen.clear(); }

void FogField::syncCloud(std::uint32_t id, const glm::vec3& center, float radius, float height, float age,
                         float lifetime) {
    m_seen.push_back(id);
    auto it = std::find_if(m_clouds.begin(), m_clouds.end(), [id](const FogCloud& c) { return c.id == id; });
    if (it == m_clouds.end()) {
        m_clouds.push_back(FogCloud{id});
        it = m_clouds.end() - 1;
    }
    it->center = center;
    it->radius = radius;
    it->height = height;
    it->age = age;
    it->lifetime = lifetime;
}

void FogField::setReach(std::uint32_t id, const FogReach& reach, const glm::vec3& at) {
    for (FogCloud& c : m_clouds) {
        if (c.id == id) {
            c.reach = reach;
            c.reachCenter = at;
            c.measured = true;
        }
    }
}

void FogField::endSync() {
    std::erase_if(m_clouds, [this](const FogCloud& c) {
        return std::find(m_seen.begin(), m_seen.end(), c.id) == m_seen.end();
    });
    if (m_clouds.empty()) {
        m_holes.clear();
    }
}

const FogCloud* FogField::cloud(std::uint32_t id) const {
    const auto it = std::find_if(m_clouds.begin(), m_clouds.end(), [id](const FogCloud& c) { return c.id == id; });
    return it == m_clouds.end() ? nullptr : &*it;
}

float FogField::density(const glm::vec3& point) const {
    float density = 0.0f;
    for (const FogCloud& c : m_clouds) {
        density = std::max(density, fogCloudDensity(c, point));
    }
    if (density <= 0.0f) {
        return 0.0f;
    }
    for (const FogHole& h : m_holes) {
        density *= fogHoleClearing(h, point);
    }
    return density;
}

float FogField::transmittance(const glm::vec3& from, const glm::vec3& to) const {
    if (m_clouds.empty()) {
        return 1.0f;
    }
    const float length = glm::distance(from, to);
    const float step = length / static_cast<float>(kSightSamples);
    float optical = 0.0f;
    for (int i = 0; i < kSightSamples; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) / static_cast<float>(kSightSamples);
        optical += density(glm::mix(from, to, t)) * step;
    }
    return std::exp(-kExtinction * optical);
}

bool FogField::touches(const FogCloud& cloud, const glm::vec3& from, const glm::vec3& to, float margin) const {
    const glm::vec3& middle = cloud.center;
    return distanceToSegment(middle, from, to) < std::max(cloud.radius, cloud.height) + margin;
}

bool FogField::punch(const glm::vec3& from, const glm::vec3& to, float radius, float life) {
    const bool hits = std::any_of(m_clouds.begin(), m_clouds.end(),
                                  [&](const FogCloud& c) { return touches(c, from, to, radius); });
    if (!hits || life <= 0.0f || radius <= 0.0f) {
        return false;
    }
    if (m_holes.size() >= kMaxHoles) {
        const auto oldest = std::max_element(m_holes.begin(), m_holes.end(), [](const FogHole& a, const FogHole& b) {
            return a.age / a.life < b.age / b.life;
        });
        m_holes.erase(oldest);
    }

    if (!m_holes.empty() && from != to) {
        FogHole& last = m_holes.back();
        const glm::vec3 a = last.to - last.from;
        const glm::vec3 b = to - from;
        if (last.age < 0.05f && glm::distance(last.to, from) < 1e-3f && glm::length(a) > 1e-4f &&
            glm::length(glm::cross(glm::normalize(a), glm::normalize(b))) < 0.02f) {
            last.to = to;
            return true;
        }
    }
    m_holes.push_back(FogHole{from, to, radius, 0.0f, life});
    return true;
}

std::vector<std::uint32_t> FogField::electrify(const glm::vec3& from, const glm::vec3& to, float duration) {
    std::vector<std::uint32_t> charged;
    for (FogCloud& c : m_clouds) {
        bool through = false;
        for (int i = 0; i <= 16 && !through; ++i) {
            through = fogCloudDensity(c, glm::mix(from, to, static_cast<float>(i) / 16.0f)) > 0.1f;
        }
        if (!through) {
            continue;
        }
        if (c.electrified <= 0.0f) {
            charged.push_back(c.id);
        }
        c.electrified = std::max(c.electrified, duration);
    }
    return charged;
}

void FogField::pull(std::uint32_t id, const glm::vec3& point, float amount) {
    for (FogCloud& c : m_clouds) {
        if (c.id == id) {
            c.pullPoint = point;
            c.pull = std::min(1.0f, c.pull + amount);
        }
    }
}

void FogField::tick(float dt) {
    for (FogHole& h : m_holes) {
        h.age += dt;
    }
    std::erase_if(m_holes, [](const FogHole& h) { return h.age >= h.life; });
    for (FogCloud& c : m_clouds) {
        c.electrified = std::max(0.0f, c.electrified - dt);
        c.pull = std::max(0.0f, c.pull - 0.5f * dt);
        c.pullPhase += c.pull * dt;
    }
}

}
