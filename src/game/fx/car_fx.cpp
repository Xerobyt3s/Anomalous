#include "game/fx/car_fx.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {

CarFxTuning parseCarFxTuning(std::string_view json) {
    CarFxTuning t;
    const nlohmann::json doc = nlohmann::json::parse(json, nullptr, false);
    if (!doc.is_object()) {
        return t;
    }
    if (const auto smoke = doc.find("smoke"); smoke != doc.end() && smoke->is_object()) {
        t.smokeRate = smoke->value("rate", t.smokeRate);
        t.smokeLife = smoke->value("life", t.smokeLife);
        t.smokeStartRadius = smoke->value("startRadius", t.smokeStartRadius);
        t.smokeEndRadius = smoke->value("endRadius", t.smokeEndRadius);
        t.smokeRise = smoke->value("rise", t.smokeRise);
        t.smokeDrag = smoke->value("drag", t.smokeDrag);
        t.dustDensity = smoke->value("dustDensity", t.dustDensity);
        t.smokeThickness = smoke->value("thickness", t.smokeThickness);
    }
    if (const auto steam = doc.find("steam"); steam != doc.end() && steam->is_object()) {
        t.steamRate = steam->value("rate", t.steamRate);
        t.steamLife = steam->value("life", t.steamLife);
        t.steamRise = steam->value("rise", t.steamRise);
        t.steamStartRadius = steam->value("startRadius", t.steamStartRadius);
        t.steamEndRadius = steam->value("endRadius", t.steamEndRadius);
    }
    if (const auto skid = doc.find("skid"); skid != doc.end() && skid->is_object()) {
        t.driftAngleStart = skid->value("driftAngleStart", t.driftAngleStart);
        t.driftAngleFull = skid->value("driftAngleFull", t.driftAngleFull);
        t.steeredAngleExtra = skid->value("steeredAngleExtra", t.steeredAngleExtra);
        t.spinRatioStart = skid->value("spinRatioStart", t.spinRatioStart);
        t.spinRatioFull = skid->value("spinRatioFull", t.spinRatioFull);
    }
    if (const auto marks = doc.find("marks"); marks != doc.end() && marks->is_object()) {
        t.markThreshold = marks->value("threshold", t.markThreshold);
        t.markHalfWidth = marks->value("halfWidth", t.markHalfWidth);
        t.markMaxLength = marks->value("maxLength", t.markMaxLength);
        t.markTurnDeg = marks->value("turnDeg", t.markTurnDeg);
        t.markJump = marks->value("jump", t.markJump);
        t.markFadeStart = marks->value("fadeStart", t.markFadeStart);
        t.markLife = std::max(marks->value("life", t.markLife), t.markFadeStart + 0.1f);
    }
    return t;
}

float skidIntensity(const WheelSample& wheel, const CarFxTuning& t) {
    if (!wheel.grounded) {
        return 0.0f;
    }
    const float angle = glm::degrees(std::abs(wheel.slipAngle)) - (wheel.steered ? t.steeredAngleExtra : 0.0f);
    const float drift = std::clamp((angle - t.driftAngleStart) / std::max(t.driftAngleFull - t.driftAngleStart, 0.1f), 0.0f, 1.0f) *
                        std::clamp((std::abs(wheel.slideLat) - 1.0f) / 2.0f, 0.0f, 1.0f);
    const float spin = std::clamp((std::abs(wheel.slipRatio) - t.spinRatioStart) / std::max(t.spinRatioFull - t.spinRatioStart, 0.01f),
                                  0.0f, 1.0f) *
                       std::clamp((std::abs(wheel.slideLong) - 1.5f) / 3.0f, 0.0f, 1.0f);
    return std::max(drift, spin);
}

float CarPuff::radius() const {
    const float t = std::clamp(age / std::max(life, 1e-3f), 0.0f, 1.0f);
    return startRadius + (endRadius - startRadius) * (1.0f - (1.0f - t) * (1.0f - t));
}

float CarPuff::opacity() const {
    const float t = std::clamp(age / std::max(life, 1e-3f), 0.0f, 1.0f);
    const float in = std::clamp(age * 8.0f, 0.0f, 1.0f);
    return density * in * (1.0f - t) * (1.0f - t);
}

float CarSmoke::random() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

void CarSmoke::spawn(const CarPuff& puff) {
    if (m_puffs.size() < kMaxPuffs) {
        m_puffs.push_back(puff);
        return;
    }
    m_puffs[m_oldest] = puff;
    m_oldest = (m_oldest + 1) % kMaxPuffs;
}

void CarSmoke::emitWheels(std::span<const WheelSample> wheels, const glm::vec3& carVelocity, const glm::vec3& up,
                          float dt) {
    for (std::size_t i = 0; i < wheels.size() && i < kWheels; ++i) {
        const WheelSample& w = wheels[i];
        const float intensity = skidIntensity(w, m_tuning);
        if (intensity < m_tuning.markThreshold) {
            m_wheelDue[i] = 0.0f;
            continue;
        }
        m_wheelDue[i] += m_tuning.smokeRate * intensity * dt;
        while (m_wheelDue[i] >= 1.0f) {
            m_wheelDue[i] -= 1.0f;
            const glm::vec3 jitter{random() - 0.5f, random() - 0.5f, random() - 0.5f};
            CarPuff puff;
            puff.kind = w.road ? PuffKind::Tire : PuffKind::Dust;
            puff.center = w.contact + up * 0.18f + jitter * 0.2f;
            puff.velocity = carVelocity * 0.25f + up * m_tuning.smokeRise + jitter * 0.6f;
            puff.life = m_tuning.smokeLife * (0.75f + 0.5f * random());
            puff.startRadius = m_tuning.smokeStartRadius;
            puff.endRadius = m_tuning.smokeEndRadius * (0.8f + 0.4f * random());
            puff.density = (w.road ? 1.0f : m_tuning.dustDensity) * (0.5f + 0.5f * intensity);
            puff.seed = random() * 100.0f;
            spawn(puff);
        }
    }
}

void CarSmoke::emitSteam(const glm::vec3& at, float heat, const glm::vec3& up, float dt) {
    if (heat <= 0.0f) {
        m_steamDue = 0.0f;
        return;
    }
    m_steamDue += m_tuning.steamRate * heat * dt;
    while (m_steamDue >= 1.0f) {
        m_steamDue -= 1.0f;
        const glm::vec3 jitter{random() - 0.5f, random() - 0.5f, random() - 0.5f};
        CarPuff puff;
        puff.kind = PuffKind::Steam;
        puff.center = at + jitter * glm::vec3(0.5f, 0.05f, 0.4f);
        puff.velocity = up * m_tuning.steamRise + jitter * 0.4f;
        puff.life = m_tuning.steamLife * (0.75f + 0.5f * random());
        puff.startRadius = m_tuning.steamStartRadius;
        puff.endRadius = m_tuning.steamEndRadius;
        puff.density = 0.8f;
        puff.seed = random() * 100.0f;
        spawn(puff);
    }
}

void CarSmoke::update(float dt, const glm::vec3& up, const glm::vec3& wind) {
    const float drag = std::exp(-m_tuning.smokeDrag * dt);
    for (CarPuff& p : m_puffs) {
        p.age += dt;
        p.velocity = wind + (p.velocity - wind) * drag + up * (0.15f * dt);
        p.center += p.velocity * dt;
    }
    const auto dead = std::remove_if(m_puffs.begin(), m_puffs.end(), [](const CarPuff& p) { return p.age >= p.life; });
    if (dead != m_puffs.end()) {
        m_puffs.erase(dead, m_puffs.end());
        m_oldest = 0;
    }
}

void CarSmoke::clear() {
    m_puffs.clear();
    m_oldest = 0;
    for (float& due : m_wheelDue) {
        due = 0.0f;
    }
    m_steamDue = 0.0f;
}

std::size_t SkidMarks::open(const glm::vec3& at, const WheelSample& sample, float strength) {
    std::size_t index;
    if (m_segments.size() < kMaxSegments) {
        index = m_segments.size();
        m_segments.emplace_back();
    } else {
        index = m_next;
        m_next = (m_next + 1) % kMaxSegments;
        for (Trail& trail : m_trails) {
            if (trail.active && trail.segment == index) {
                trail.active = false;
            }
        }
    }
    SkidSegment& s = m_segments[index];
    s = SkidSegment{};
    s.halfWidth = m_tuning.markHalfWidth;
    stretch(s, at, at, sample, strength);
    return index;
}

void SkidMarks::stretch(SkidSegment& segment, const glm::vec3& start, const glm::vec3& end, const WheelSample& sample,
                        float strength) {
    const glm::vec3 d = end - start;
    const float length = glm::length(d);
    segment.center = (start + end) * 0.5f;
    segment.normal = sample.normal;
    if (length > 0.05f) {
        segment.along = d / length;
    } else {
        const glm::vec3 flat = sample.forward - sample.normal * glm::dot(sample.forward, sample.normal);
        if (glm::dot(flat, flat) > 1e-6f) {
            segment.along = glm::normalize(flat);
        }
    }
    segment.halfLength = std::max(length * 0.5f, segment.halfWidth * 0.6f);
    segment.strength = std::max(segment.strength, strength);
    segment.road = sample.road;
    segment.age = 0.0f;
}

void SkidMarks::sample(std::size_t wheel, const WheelSample& sample) {
    if (wheel >= kWheels) {
        return;
    }
    Trail& trail = m_trails[wheel];
    const float strength = skidIntensity(sample, m_tuning);
    const float keep = trail.active ? m_tuning.markThreshold * 0.4f : m_tuning.markThreshold;
    if (!sample.grounded || strength < keep) {
        trail.active = false;
        trail.pending = 0;
        return;
    }
    if (!trail.active && ++trail.pending < kStartTicks) {
        return;
    }
    const glm::vec3 at = sample.contact;
    if (!trail.active || glm::distance(at, trail.last) > m_tuning.markJump) {
        trail.active = true;
        trail.start = at;
        trail.last = at;
        trail.segment = open(at, sample, strength);
        return;
    }
    const glm::vec3 span = at - trail.start;
    const float length = glm::length(span);
    SkidSegment& current = m_segments[trail.segment];
    const bool turned = length > 0.2f && glm::dot(span / length, current.along) <
                                             std::cos(glm::radians(m_tuning.markTurnDeg));
    if (length > m_tuning.markMaxLength || turned) {
        trail.start = trail.last;
        trail.segment = open(trail.start, sample, strength);
    }
    stretch(m_segments[trail.segment], trail.start, at, sample, strength);
    trail.last = at;
}

void SkidMarks::update(float dt) {
    for (SkidSegment& s : m_segments) {
        s.age += dt;
    }
}

float SkidMarks::fade(const SkidSegment& segment) const {
    const float t = std::clamp((segment.age - m_tuning.markFadeStart) /
                                   std::max(m_tuning.markLife - m_tuning.markFadeStart, 0.1f),
                               0.0f, 1.0f);
    return 1.0f - t;
}

void SkidMarks::clear() {
    m_segments.clear();
    m_next = 0;
    for (Trail& trail : m_trails) {
        trail.active = false;
    }
}

}
