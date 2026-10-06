#include "game/player/player_hit.h"

#include "game/world/element_volume.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
glm::vec3 headCenter(const PlayerBody& body) {
    return bodyPoint(body, std::max(body.height - kHeadRadius, kHeadRadius));
}

glm::vec3 bodyPoint(const PlayerBody& body, float along) {
    return body.feet + body.up * along;
}

std::optional<float> segmentCapsule(const glm::vec3& from, const glm::vec3& to, const glm::vec3& feet, float radius,
                                    float height, const glm::vec3& up) {
    const float core = std::max(height - 2.0f * radius, 0.0f);
    const glm::vec3 a = feet + up * radius;
    const glm::vec3 b = a + up * core;
    std::optional<float> best;
    auto consider = [&best](std::optional<float> t) {
        if (t && (!best || *t < *best)) {
            best = t;
        }
    };
    consider(segmentSphere(from, to, a, radius));
    consider(segmentSphere(from, to, b, radius));

    const glm::vec3 d = to - from;
    const glm::vec3 o = from - a;
    const glm::vec3 dPerp = d - up * glm::dot(d, up);
    const glm::vec3 oPerp = o - up * glm::dot(o, up);
    const float qa = glm::dot(dPerp, dPerp);
    if (qa > 1e-12f) {
        const float qb = glm::dot(oPerp, dPerp);
        const float qc = glm::dot(oPerp, oPerp) - radius * radius;
        const float disc = qb * qb - qa * qc;
        if (disc >= 0.0f && qc > 0.0f) {
            const float t = (-qb - std::sqrt(disc)) / qa;
            const float along = glm::dot(o + d * t, up);
            if (t >= 0.0f && t <= 1.0f && along >= 0.0f && along <= core) {
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
        const auto bodyAt = segmentCapsule(from, to, body.feet, body.radius, body.height, body.up);
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
        const glm::vec3 axis = bodyPoint(body, std::clamp(glm::dot(hit.point - body.feet, body.up), body.radius,
                                                          std::max(body.height - body.radius, body.radius)));
        const glm::vec3 out = hit.point - axis;
        hit.normal = glm::length(out) > 1e-4f ? glm::normalize(out) : glm::normalize(from - to);
        best = hit;
    }
    return best;
}

}

namespace ghost::game {
glm::vec3 blastShove(const glm::vec3& center, const PlayerBody& body, float radius, float knockback) {
    const float along = glm::clamp(glm::dot(center - body.feet, body.up), 0.0f, body.height);
    const float distance = glm::distance(center, bodyPoint(body, along));
    if (radius <= 0.0f || distance >= radius) {
        return glm::vec3(0.0f);
    }
    glm::vec3 away = bodyPoint(body, body.height * 0.5f) - center;
    away = glm::length(away) > 1e-4f ? glm::normalize(away) : body.up;
    return away * (knockback * (1.0f - 0.5f * distance / radius));
}

}
