#include "game/fx/material_shapes.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
float hash(float n) {
    const float s = std::sin(n * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

void basisAround(const glm::vec3& axis, glm::vec3& u, glm::vec3& v) {
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    u = glm::normalize(glm::cross(axis, helper));
    v = glm::cross(axis, u);
}

float smooth01(float x) {
    const float t = glm::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 sideways(const glm::vec3& along) {
    const glm::vec3 side = glm::cross(along, glm::vec3(0.0f, 1.0f, 0.0f));
    const float length = glm::length(side);
    return length > 1e-5f ? side / length : glm::vec3(1.0f, 0.0f, 0.0f);
}

}

std::vector<Strands::Point> windRibbon(int index, const glm::vec3& center, float time, float seed, float detach,
                                       const glm::vec3& target) {
    constexpr int kPoints = 16;
    constexpr float kOrbit = 0.085f;
    constexpr float kArc = 1.9f;
    const float fi = static_cast<float>(index);
    const glm::vec3 axis = glm::normalize(glm::vec3(std::sin(fi * 2.1f + seed), 1.6f, std::cos(fi * 1.7f + seed)));
    glm::vec3 u;
    glm::vec3 v;
    basisAround(axis, u, v);
    const float speed = 3.2f + 0.8f * fi;
    const float head = time * speed + fi * 1.6f + seed;
    const float radius = kOrbit * (1.0f + 0.15f * std::sin(time * 1.3f + fi));
    const glm::vec3 bendWay = sideways(target - center);
    std::vector<Strands::Point> points;
    points.reserve(kPoints);
    for (int j = 0; j < kPoints; ++j) {
        const float along = static_cast<float>(j) / static_cast<float>(kPoints - 1);
        const float angle = head - along * kArc;
        glm::vec3 at = center + (u * std::cos(angle) + v * std::sin(angle)) * radius + axis * (0.012f * std::sin(angle * 2.0f + fi));

        const float k = smooth01(detach * 1.6f - along * 0.6f);
        if (k > 0.0f) {
            at = glm::mix(at, target, k) + bendWay * (std::sin(k * glm::pi<float>()) * 0.12f * (index % 2 == 0 ? 1.0f : -1.0f));
        }
        Strands::Point p;
        p.position = at;
        p.width = 0.016f * std::sin(glm::pi<float>() * (0.15f + 0.85f * along)) * (1.0f - 0.7f * smooth01(detach * 1.2f - 0.4f));
        p.color = glm::vec3(0.82f, 0.93f, 1.0f);
        p.alpha = 0.22f * (1.0f - along);
        p.glow = 0.45f * (1.0f - along) * (1.0f - smooth01((detach - 0.6f) * 2.5f));
        points.push_back(p);
    }
    return points;
}

void threadKnot(const glm::vec3& center, float time, float seed, float unspool, const glm::vec3& target,
                std::vector<std::vector<Strands::Point>>& out) {
    constexpr int kLoopPoints = 40;
    constexpr float kBall = 0.032f;
    constexpr float kWidth = 0.003f;
    const glm::vec3 kThread{0.24f, 0.07f, 0.3f};
    const glm::vec3 kPulse{1.0f, 0.12f, 0.08f};

    const float twitch = std::pow(std::max(0.0f, std::sin((time * 0.8f + seed) * glm::two_pi<float>())), 24.0f) * 0.35f;
    const float spin = time * 0.6f + twitch;
    const float cs = std::cos(spin);
    const float sn = std::sin(spin);
    auto turn = [&](const glm::vec3& p) { return glm::vec3(p.x * cs + p.z * sn, p.y, -p.x * sn + p.z * cs); };
    auto bead = [&](const glm::vec3& at, float pulse) {
        Strands::Point p;
        p.position = at;
        p.width = kWidth;
        p.color = glm::mix(kThread, kPulse, std::min(pulse, 1.0f));
        p.alpha = 1.0f;
        p.glow = 1.8f * pulse;
        return p;
    };

    struct Bead {
        glm::vec3 position;
        float pulse;
    };
    std::vector<Bead> thread;
    thread.reserve(kKnotLoops * kLoopPoints);
    const float shrink = 1.0f - 0.6f * smooth01(unspool);
    for (int loop = 0; loop < kKnotLoops; ++loop) {
        const float fl = static_cast<float>(loop) + seed * 3.1f;
        glm::vec3 axis{hash(fl) - 0.5f, hash(fl + 7.0f) - 0.5f, hash(fl + 13.0f) - 0.5f};
        axis = glm::length(axis) > 1e-3f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 u;
        glm::vec3 v;
        basisAround(axis, u, v);

        const float pulseAt = std::fmod(time * (0.35f + 0.25f * hash(fl + 3.0f)) + hash(fl + 5.0f), 1.0f);
        for (int j = 0; j < kLoopPoints; ++j) {
            const float s = static_cast<float>(j) / static_cast<float>(kLoopPoints);
            const float angle = s * glm::two_pi<float>();
            const float wobble = 1.0f + 0.18f * std::sin(angle * 3.0f + fl) + 0.08f * std::sin(angle * 5.0f - fl * 2.0f);
            const glm::vec3 local = (u * std::cos(angle) + v * std::sin(angle)) * (kBall * wobble * shrink);
            float gap = std::abs(s - pulseAt);
            gap = std::min(gap, 1.0f - gap);
            thread.push_back({center + turn(local), std::exp(-gap * gap / 0.004f)});
        }
    }
    const int total = static_cast<int>(thread.size());

    if (unspool <= 0.0f) {
        for (int loop = 0; loop < kKnotLoops; ++loop) {
            std::vector<Strands::Point> ring;
            ring.reserve(kLoopPoints + 1);
            for (int j = 0; j <= kLoopPoints; ++j) {
                const Bead& b = thread[static_cast<std::size_t>(loop * kLoopPoints + j % kLoopPoints)];
                ring.push_back(bead(b.position, b.pulse));
            }
            out.push_back(std::move(ring));
        }
        return;
    }

    const int pulled = std::min(total, static_cast<int>(static_cast<float>(total) * smooth01((unspool - 0.08f) / 0.82f)));

    const glm::vec3 exit = pulled < total ? thread[static_cast<std::size_t>(pulled)].position : center;
    const glm::vec3 from = glm::mix(exit, target, smooth01((unspool - 0.85f) / 0.15f));
    const glm::vec3 tip = glm::mix(from, target, smooth01(unspool / 0.3f));
    const glm::vec3 across = sideways(target - center);

    const float lastPulse = smooth01((unspool - 0.55f) / 0.4f);
    constexpr int kFree = 32;
    std::vector<Strands::Point> strand;
    strand.reserve(kFree);
    for (int j = 0; j < kFree; ++j) {
        const float w = static_cast<float>(j) / static_cast<float>(kFree - 1);
        const float whip = std::sin(w * glm::pi<float>()) * std::sin(w * 9.0f - time * 24.0f) * 0.03f * (1.0f - unspool);
        const float gap = w - lastPulse;
        strand.push_back(bead(glm::mix(from, tip, w) + across * whip, lastPulse > 0.0f ? std::exp(-gap * gap / 0.006f) : 0.0f));
    }
    out.push_back(std::move(strand));

    if (total - pulled >= 2) {
        std::vector<Strands::Point> knot;
        knot.reserve(static_cast<std::size_t>(total - pulled));
        for (int j = pulled; j < total; ++j) {
            knot.push_back(bead(thread[static_cast<std::size_t>(j)].position, thread[static_cast<std::size_t>(j)].pulse));
        }
        out.push_back(std::move(knot));
    }
}

}
