#include "game/player/player_hit.h"

#include "game/world/element_volume.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
glm::vec3 headCenter(const PlayerBody& body) {
    return body.feet + glm::vec3(0.0f, std::max(body.height - kHeadRadius, kHeadRadius), 0.0f);
}

std::optional<float> segmentCapsule(const glm::vec3& from, const glm::vec3& to, const glm::vec3& feet, float radius,
                                    float height) {
    const float core = std::max(height - 2.0f * radius, 0.0f);
    const glm::vec3 a = feet + glm::vec3(0.0f, radius, 0.0f);
    const glm::vec3 b = a + glm::vec3(0.0f, core, 0.0f);
    std::optional<float> best;
    auto consider = [&best](std::optional<float> t) {
        if (t && (!best || *t < *best)) {
            best = t;
        }
    };
    consider(segmentSphere(from, to, a, radius));
    consider(segmentSphere(from, to, b, radius));

    const glm::vec3 d = to - from;
    const glm::vec2 d2{d.x, d.z};
    const glm::vec2 o2{from.x - a.x, from.z - a.z};
    const float qa = glm::dot(d2, d2);
    if (qa > 1e-12f) {
        const float qb = glm::dot(o2, d2);
        const float qc = glm::dot(o2, o2) - radius * radius;
        const float disc = qb * qb - qa * qc;
        if (disc >= 0.0f && qc > 0.0f) {
            const float t = (-qb - std::sqrt(disc)) / qa;
            const float y = from.y + d.y * t;
            if (t >= 0.0f && t <= 1.0f && y >= a.y && y <= b.y) {
                consider(t);
            }
        }
    }
    return best;
}

std::optional<PlayerRayHit> raycastPlayers(std::span<const PlayerBody> players, const glm::vec3& from, const glm::vec3& to,
                                           PlayerId ignore) {
    std::optional<PlayerRayHit> best;
    for (const PlayerBody& body : players) {
        if (body.id == ignore || body.possessed) {
            continue;
        }
        const auto bodyAt = segmentCapsule(from, to, body.feet, body.radius, body.height);
        const auto headAt = body.downed ? std::nullopt : segmentSphere(from, to, headCenter(body), kHeadRadius);
        if (!bodyAt && !headAt) {
            continue;
        }

        const bool head = headAt.has_value();
        const float t = head ? std::min(*headAt, bodyAt.value_or(*headAt)) : *bodyAt;
        if (best && best->fraction <= t) {
            continue;
        }
        PlayerRayHit hit;
        hit.id = body.id;
        hit.fraction = t;
        hit.point = glm::mix(from, to, t);
        hit.head = head;
        const glm::vec3 axis{body.feet.x, std::clamp(hit.point.y, body.feet.y + body.radius, body.feet.y + std::max(body.height - body.radius, body.radius)),
                             body.feet.z};
        const glm::vec3 out = hit.point - axis;
        hit.normal = glm::length(out) > 1e-4f ? glm::normalize(out) : glm::normalize(from - to);
        best = hit;
    }
    return best;
}

}

namespace ghost::game {
glm::vec3 blastShove(const glm::vec3& center, const PlayerBody& body, float radius, float knockback) {
    const float along = glm::clamp(center.y - body.feet.y, 0.0f, body.height);
    const float distance = glm::distance(center, body.feet + glm::vec3(0.0f, along, 0.0f));
    if (radius <= 0.0f || distance >= radius) {
        return glm::vec3(0.0f);
    }
    glm::vec3 away = body.feet + glm::vec3(0.0f, body.height * 0.5f, 0.0f) - center;
    away = glm::length(away) > 1e-4f ? glm::normalize(away) : glm::vec3(0.0f, 1.0f, 0.0f);
    return away * (knockback * (1.0f - 0.5f * distance / radius));
}

}
