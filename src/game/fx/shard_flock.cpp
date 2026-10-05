#include "game/fx/shard_flock.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr int kMost = 440;
constexpr float kChangeTime = 0.6f;
constexpr float kChangeSpread = 0.3f;

float hash(float n) {
    const float s = std::sin(n * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-5f ? v / length : fallback;
}

float smooth01(float x) {
    const float t = glm::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 onSphere(int i, int n) {
    const float y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(std::max(n, 1));
    const float r = std::sqrt(std::max(0.0f, 1.0f - y * y));
    const float a = static_cast<float>(i) * 2.399963f;
    return {r * std::cos(a), y, r * std::sin(a)};
}

}

int ShardFlock::countFor(float size) { return std::clamp(static_cast<int>(std::lround(static_cast<float>(kFull) * size)), 8, kMost); }

ShardFlock::Slot ShardFlock::slot(int index, int count, Form form, const Input& in, float scale) const {
    const float fi = static_cast<float>(index);
    const float h1 = hash(fi * 1.7f + in.seed);
    const float h2 = hash(fi * 3.1f + in.seed + 5.0f);
    const float h3 = hash(fi * 5.3f + in.seed + 11.0f);
    switch (form) {
    case Form::Cluster: {
        const glm::vec3 n = normalizeOr(in.normal, kUp);
        const glm::vec3 a = normalizeOr(glm::cross(n, std::abs(n.y) < 0.9f ? kUp : glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 b = glm::cross(n, a);
        const float angle = h1 * glm::two_pi<float>();
        const float splay = std::sqrt(h2) * 1.1f;
        const glm::vec3 out = glm::normalize(n + (a * std::cos(angle) + b * std::sin(angle)) * splay);
        const float reach = (0.08f + 0.3f * (1.0f - splay * 0.6f) * h3) * scale;
        return {in.center - n * (0.12f * scale) + (a * std::cos(angle) + b * std::sin(angle)) * (splay * 0.16f * scale) + out * reach, out,
                0.8f + 0.5f * (1.0f - splay * 0.6f)};
    }
    case Form::Ball: {
        const glm::vec3 out = onSphere(index, count);
        return {in.center + out * ((0.2f + 0.12f * h1) * scale), out, 1.0f};
    }
    case Form::Cloud: {
        const glm::vec3 axis = normalizeOr(glm::vec3(h1 - 0.5f, 0.6f + h2, h3 - 0.5f), kUp);
        const glm::vec3 a = normalizeOr(glm::cross(axis, glm::vec3(0.3f, 0.1f, 1.0f)), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 b = glm::cross(axis, a);
        const float angle = m_time * (2.2f + 2.8f * h2) * (index % 2 == 0 ? 1.0f : -0.7f) + h3 * glm::two_pi<float>();
        const float radius = (0.55f + 1.05f * h1) * scale;
        const glm::vec3 offset = (a * std::cos(angle) + b * std::sin(angle)) * radius + axis * ((h3 - 0.5f) * 0.7f * scale);
        return {in.center + offset, normalizeOr(b * std::cos(angle) - a * std::sin(angle), axis), 0.9f};
    }
    case Form::Raven:
    case Form::Stoop:
    default: {
        const bool stoop = form == Form::Stoop;

        const float flare = stoop ? 0.0f : m_flare;
        const glm::vec3 level = normalizeOr(glm::cross(m_forward, kUp), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 over = glm::cross(level, m_forward);
        const glm::vec3 f = glm::normalize(m_forward * std::cos(flare * 0.7f) + over * std::sin(flare * 0.7f));
        const glm::vec3 above = glm::cross(level, f);
        const glm::vec3 r = level * std::cos(m_bank) + above * std::sin(m_bank);
        const glm::vec3 u = above * std::cos(m_bank) - level * std::sin(m_bank);
        const float part = fi / static_cast<float>(std::max(count, 1));
        if (part < 0.30f) {
            const float g = h1;
            const float along = glm::mix(-0.36f, 0.3f, g);
            const float girth = 0.15f * std::pow(std::sin(glm::pi<float>() * (0.08f + 0.88f * g)), 0.6f) * (0.5f + 0.5f * g);
            const float angle = h2 * glm::two_pi<float>();
            return {in.center + (f * along + (r * std::cos(angle) + u * (std::sin(angle) * 0.85f)) * (girth * std::sqrt(h3))) * scale, f, 0.7f};
        }
        if (part < 0.38f) {
            const glm::vec3 round = onSphere(index % 19, 19);
            return {in.center + (f * 0.4f + u * 0.05f + round * (0.085f * std::sqrt(h1))) * scale, normalizeOr(f + round * 0.5f, f), 0.55f};
        }
        if (part < 0.41f) {
            return {in.center + (f * (0.47f + 0.15f * h1) + u * (0.04f - 0.02f * h1)) * scale, f, 0.6f};
        }
        if (part < 0.53f) {
            const float fan = (h1 - 0.5f) * (stoop ? 0.12f : 0.55f + 0.3f * flare);
            const float back = 0.34f + 0.38f * h2;
            const glm::vec3 direction = glm::normalize(-f + r * fan);
            return {in.center + (direction * back - u * 0.02f) * scale, direction, 1.25f};
        }

        const float side = index % 2 == 0 ? 1.0f : -1.0f;
        const float s = std::pow(h1, 0.8f);
        const float c = h2;
        const glm::vec3 out = r * side;
        const glm::vec3 shoulder = f * 0.1f + out * 0.09f;
        const glm::vec3 wrist = shoulder + (stoop ? out * 0.07f - f * 0.2f : out * 0.31f + f * 0.09f);
        const glm::vec3 tip = wrist + (stoop ? out * 0.02f - f * 0.5f : out * 0.34f - f * 0.2f);
        const glm::vec3 bone = s < 0.5f ? glm::mix(shoulder, wrist, s / 0.5f) : glm::mix(wrist, tip, (s - 0.5f) / 0.5f);
        const float chord = (stoop ? 0.1f : 0.25f) * (1.0f - 0.55f * s);
        glm::vec3 at = bone - f * (c * chord);
        if (!stoop) {
            const float swing = m_beatDepth * std::sin(m_beat - s * 1.1f) * (0.35f + 0.65f * s) + 0.75f * flare * (0.4f + 0.6f * s);
            const glm::vec3 arm = at - shoulder;
            const float reach = glm::dot(arm, out);
            at = shoulder + (arm - out * reach) + (out * std::cos(swing) + u * std::sin(swing)) * reach;
        }
        const glm::vec3 boneWay = normalizeOr((s < 0.5f ? wrist - shoulder : tip - wrist), out);
        const glm::vec3 feather = c < 0.35f ? normalizeOr(boneWay - f * 0.3f, boneWay)
                                            : normalizeOr(-f + out * (s < 0.5f ? 0.3f : 0.85f), -f);
        return {in.center + at * scale, feather, c < 0.35f ? 0.6f : 1.1f + 0.35f * s};
    }
    }
}

void ShardFlock::update(float dtIn, const Input& in) {
    const float dt = std::min(dtIn, 1.0f / 30.0f);
    m_time += dt;
    if (!m_started) {
        m_lastCenter = in.center;
        m_from = m_to = in.form;
        m_change = 10.0f;
    }
    const glm::vec3 moved = in.center - m_lastCenter;
    m_lastCenter = in.center;

    const glm::vec3 before = m_forward;
    if (glm::length(in.velocity) > 0.8f) {
        m_forward = normalizeOr(glm::mix(m_forward, glm::normalize(in.velocity), std::min(1.0f, dt * 5.0f)), m_forward);
    }
    const float turn = (before.z * m_forward.x - before.x * m_forward.z) / std::max(dt, 1e-4f);
    m_bank += (glm::clamp(turn * 0.35f, -0.6f, 0.6f) - m_bank) * std::min(1.0f, dt * 4.0f);
    m_flare += (glm::clamp(in.flare, 0.0f, 1.0f) - m_flare) * std::min(1.0f, dt * 6.0f);

    const float glide = smooth01(0.5f + 1.6f * std::sin(m_time * 0.9f + in.seed));
    m_beatDepth += (glm::mix(0.12f, 0.6f, std::max(glide, m_flare)) - m_beatDepth) * std::min(1.0f, dt * 3.0f);
    m_beat += dt * glm::two_pi<float>() * (3.0f + 1.0f * m_flare) / std::max(std::cbrt(std::clamp(in.size, 0.3f, 1.5f)), 0.6f);

    if (in.form != m_to) {
        m_from = m_to;
        m_to = in.form;
        m_change = 0.0f;
    }
    m_change += dt;

    const int wanted = countFor(in.size);
    while (static_cast<int>(m_shards.size()) > wanted) {
        Shard lost = m_shards.back();
        m_shards.pop_back();
        lost.velocity += glm::vec3(hash(lost.roll) - 0.5f, 0.6f, hash(lost.roll + 3.0f) - 0.5f) * 2.5f;
        m_debris.push_back(lost);
    }
    while (static_cast<int>(m_shards.size()) < wanted) {
        const float fi = static_cast<float>(m_shards.size());
        Shard shard;
        shard.base = 0.12f + 0.2f * hash(fi * 2.3f + in.seed);
        shard.length = shard.base;
        shard.slim = 0.3f + 0.16f * hash(fi * 4.1f + in.seed);
        shard.width = shard.base * shard.slim;
        shard.roll = hash(fi * 7.7f + in.seed) * glm::two_pi<float>();
        shard.position = in.center + onSphere(static_cast<int>(m_shards.size()), wanted) * (m_started ? 0.9f : 0.2f);
        m_shards.push_back(shard);
    }

    const float scale = std::cbrt(std::clamp(in.size, 0.2f, 1.6f));

    float stiffness = 90.0f;
    switch (in.form) {
    case Form::Cluster: stiffness = 120.0f; break;
    case Form::Ball: stiffness = 150.0f; break;
    case Form::Cloud: stiffness = 40.0f; break;
    case Form::Stoop: stiffness = 130.0f; break;
    case Form::Raven: break;
    }
    const bool changing = m_change < kChangeTime + kChangeSpread;
    stiffness *= glm::mix(0.45f, 1.0f, smooth01(m_change / (kChangeTime + kChangeSpread)));
    const float damping = 2.0f * std::sqrt(stiffness) * 0.85f;
    m_flung = std::max(0.0f, m_flung - dt);
    const int count = static_cast<int>(m_shards.size());
    for (int i = 0; i < count; ++i) {
        Shard& shard = m_shards[static_cast<std::size_t>(i)];
        Slot want = slot(i, count, m_to, in, scale);
        if (changing) {
            const float along = smooth01((m_change - kChangeSpread * hash(static_cast<float>(i) * 0.37f + in.seed)) / kChangeTime);
            if (along < 1.0f) {
                const Slot old = slot(i, count, m_from, in, scale);
                want.position = glm::mix(old.position, want.position, along);
                want.axis = normalizeOr(glm::mix(old.axis, want.axis, along), want.axis);
                want.length = glm::mix(old.length, want.length, along);
            }
        }
        if (!m_started) {
            shard.position = want.position;
            shard.axis = want.axis;
        }

        const float length = shard.base * want.length;
        shard.length += (length - shard.length) * std::min(1.0f, dt * 6.0f);
        shard.width = shard.base * shard.slim * glm::mix(1.0f, shard.length / std::max(shard.base, 1e-4f), 0.4f);
        if (m_flung > 0.0f) {
            shard.velocity *= std::exp(-2.5f * dt);
            shard.velocity.y -= 4.0f * dt;
            shard.position += shard.velocity * dt;
            shard.axis = normalizeOr(glm::mix(shard.axis, normalizeOr(shard.velocity, shard.axis), std::min(1.0f, dt * 12.0f)), shard.axis);
            continue;
        }

        shard.position += moved * 0.85f;
        const glm::vec3 pull = (want.position - shard.position) * stiffness - (shard.velocity - in.velocity * 0.15f) * damping;
        shard.velocity += pull * dt;
        const float speed = glm::length(shard.velocity);
        if (speed > 14.0f) {
            shard.velocity *= 14.0f / speed;
        }
        shard.position += shard.velocity * dt;

        const glm::vec3 facing = speed > 2.5f && m_to == Form::Cloud ? shard.velocity / speed : want.axis;
        shard.axis = normalizeOr(glm::mix(shard.axis, facing, std::min(1.0f, dt * 7.0f)), want.axis);
    }

    const bool bird = (m_to == Form::Raven || m_to == Form::Stoop) && m_flung <= 0.0f;
    const int kind = bird ? 1 : (m_to == Form::Cloud ? 2 : 0);
    if (kind != m_trailKind) {
        m_trailKind = kind;
        for (Trail& trail : m_trails) {
            trail.points.clear();
        }
    }
    m_trailClock += dt;
    if (m_trailClock >= 0.03f) {
        m_trailClock = 0.0f;
        const int active = kind == 1 ? 1 : (kind == 2 ? kTrails : 0);
        for (int k = 0; k < kTrails; ++k) {
            Trail& trail = m_trails[static_cast<std::size_t>(k)];
            if (k < active && count > 0) {
                const glm::vec3 head = kind == 1 ? in.center - m_forward * (0.5f * scale)
                                                 : m_shards[static_cast<std::size_t>(k * count / kTrails)].position;
                trail.points.insert(trail.points.begin(), head);
                trail.width = (kind == 1 ? 0.16f : 0.07f) * scale;
                if (trail.points.size() > (kind == 1 ? 22u : 14u)) {
                    trail.points.pop_back();
                }
            } else if (!trail.points.empty()) {
                trail.points.pop_back();
            }
        }
    }
    fall(dt);
    m_started = true;
}

void ShardFlock::fall(float dt) {
    for (Shard& piece : m_debris) {
        piece.velocity.y -= 9.81f * dt;
        piece.position += piece.velocity * dt;
        piece.roll += dt * 9.0f;
        piece.fade -= dt / 1.4f;
    }
    std::erase_if(m_debris, [](const Shard& s) { return s.fade <= 0.0f; });
}

ShardFlock ShardFlock::divide() {
    ShardFlock other = *this;
    other.m_debris.clear();
    other.m_shards.clear();
    std::vector<Shard> kept;
    for (std::size_t i = 0; i < m_shards.size(); ++i) {
        (i % 2 == 0 ? kept : other.m_shards).push_back(m_shards[i]);
    }
    m_shards = std::move(kept);
    return other;
}

void ShardFlock::burst(const glm::vec3& from) {
    for (std::size_t i = 0; i < m_shards.size(); ++i) {
        Shard& shard = m_shards[i];
        const glm::vec3 out = normalizeOr(shard.position - from + onSphere(static_cast<int>(i), static_cast<int>(m_shards.size())) * 0.3f, kUp);
        shard.velocity = out * (7.0f + 6.0f * hash(static_cast<float>(i) * 1.3f)) + kUp * 1.5f;
    }
    m_flung = 0.3f;
}

void ShardFlock::shatter() {
    for (Shard shard : m_shards) {
        shard.velocity += glm::vec3(hash(shard.roll) - 0.5f, 0.8f * hash(shard.roll + 1.0f), hash(shard.roll + 2.0f) - 0.5f) * 5.0f;
        m_debris.push_back(shard);
    }
    m_shards.clear();
}

void ShardFlock::takeIn(const ShardFlock& other) {
    for (const Shard& shard : other.m_shards) {
        if (static_cast<int>(m_shards.size()) < kMost) {
            m_shards.push_back(shard);
        }
    }
}

}
