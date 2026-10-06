#include "game/fx/mimic_body.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr float kStepAt = 0.27f;

glm::vec3 normalizeOr(const glm::vec3& v, const glm::vec3& fallback) {
    const float length = glm::length(v);
    return length > 1e-5f ? v / length : fallback;
}

float hash(float n) {
    const float s = std::sin(n * 12.9898f) * 43758.5453f;
    return s - std::floor(s);
}

float smooth01(float x) {
    const float t = glm::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float fract(float x) { return x - std::floor(x); }

}

float MimicBody::restLength(int tentacle) const {
    return kLength * (0.75f + 0.5f * hash(static_cast<float>(tentacle) * 3.1f + m_seed));
}

float MimicBody::thickness(int tentacle) const {
    return (0.8f + 0.5f * hash(static_cast<float>(tentacle) * 7.3f + m_seed + 2.0f)) * m_out[static_cast<std::size_t>(tentacle)];
}

float MimicBody::linkLength(int tentacle) const {
    const auto i = static_cast<std::size_t>(tentacle);
    return std::max(restLength(tentacle) * m_out[i] * (1.0f + m_stretch[i]), 0.01f) / static_cast<float>(kPoints - 1);
}

void MimicBody::update(float dtIn, const Input& in, const Raycast& raycast) {
    const float dt = std::min(dtIn, 1.0f / 30.0f);
    m_time += dt;
    m_seed = in.seed;

    const glm::vec3 clung = normalizeOr(in.surfaceNormal, glm::vec3(0.0f, 1.0f, 0.0f));
    if (!m_started) {
        m_up = clung;
        m_velocity = in.velocity;
    }
    const bool tumbling = in.mode == Mode::Tumble;
    m_up = normalizeOr(glm::mix(m_up, clung, std::min(1.0f, dt * (tumbling ? 30.0f : 10.0f))), clung);
    m_velocity += (in.velocity - m_velocity) * std::min(1.0f, dt * 9.0f);
    const glm::vec3 up = m_up;
    const glm::vec3 a = normalizeOr(glm::cross(up, std::abs(up.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)),
                                    glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 b = glm::cross(up, a);
    const float speed = glm::length(m_velocity);
    const bool air = in.mode == Mode::Air || tumbling;
    const bool ground = in.mode == Mode::Walk || in.mode == Mode::Idle || in.mode == Mode::Lash;

    for (int i = 0; i < kTentacles; ++i) {
        float& out = m_out[static_cast<std::size_t>(i)];
        if (in.mode == Mode::Hidden) {
            out = 0.0f;
        } else if (in.mode == Mode::Reveal) {
            const float order = static_cast<float>((i * 3) % kTentacles) / static_cast<float>(kTentacles);
            out = smooth01((in.progress * 1.5f - 0.15f - 0.45f * order) / 0.6f);
        } else {
            out += (1.0f - out) * std::min(1.0f, dt * 8.0f);
        }
    }
    if (in.mode == Mode::Hidden) {
        m_scale = 0.0f;
    } else if (in.mode == Mode::Reveal) {
        m_scale = smooth01(in.progress / 0.5f);
    } else {
        m_scale += (1.0f - m_scale) * std::min(1.0f, dt * 8.0f);
    }

    m_gait += dt * (1.2f + speed * 0.55f);
    const float walking = ground ? glm::clamp(speed / 3.0f, 0.0f, 1.0f) : 0.0f;
    const glm::vec3 wantCenter = in.center + up * (in.lift * m_scale + 0.025f * std::sin(m_gait * glm::two_pi<float>() * 2.0f) * walking +
                                                   0.012f * std::sin(m_time * 2.1f + in.seed));
    if (!m_started || m_scale < 0.01f || glm::distance(m_center, wantCenter) > 1.5f) {
        m_center = wantCenter;
        m_centerVelocity = m_velocity;
    } else {
        if (m_wasAir && !air) {
            m_centerVelocity -= up * 1.6f;
        }
        m_centerVelocity += ((wantCenter - m_center) * 140.0f - (m_centerVelocity - m_velocity) * 19.0f) * dt;
        m_center += m_centerVelocity * dt;
        const glm::vec3 off = m_center - wantCenter;
        if (glm::length(off) > 0.4f) {
            m_center = wantCenter + off * (0.4f / glm::length(off));
        }
    }
    m_wasAir = air;

    if (m_scale < 0.01f) {
        m_posed = 1.0f;
        m_points.fill(m_center);
        m_previous.fill(m_center);
        for (Foot& foot : m_feet) {
            foot = Foot{};
        }
        m_started = false;
        return;
    }

    glm::vec3 toTarget = in.target - m_center;
    const float targetDistance = glm::length(toTarget);
    toTarget = targetDistance > 1e-4f ? toTarget / targetDistance : -up;
    const glm::vec3 flying = normalizeOr(m_velocity, -up);
    const glm::vec3 lead = m_velocity * 0.14f;
    const float damping = std::pow(0.9f, dt * 60.0f);
    m_posed = in.mode == Mode::Reveal ? std::min(1.0f, m_posed + dt / 0.15f) : 0.0f;

    for (int i = 0; i < kTentacles; ++i) {
        const auto ti = static_cast<std::size_t>(i);
        const float fi = static_cast<float>(i);
        const float angle = fi / static_cast<float>(kTentacles) * glm::two_pi<float>() + in.seed;
        const glm::vec3 outward = a * std::cos(angle) + b * std::sin(angle);
        const glm::vec3 root = m_center + outward * (0.08f * m_scale);
        const bool feeler = i % 4 == 2;
        const bool lashing = in.mode == Mode::Lash && glm::dot(outward, toTarget) > -0.2f;
        const bool blow = lashing && in.progress >= 1.0f;
        float& stretch = m_stretch[ti];

        const float thrashing = in.mode == Mode::Reveal ? glm::clamp(1.0f / restLength(i) - 1.0f, 0.0f, 0.6f) * in.violence : 0.0f;
        if (blow) {
            stretch = std::min(0.6f, stretch + dt / 0.07f * 0.6f);
        } else if (stretch < thrashing) {
            stretch = std::min(thrashing, stretch + dt / 0.1f * 0.6f);
        } else {
            stretch = std::max(thrashing, stretch - dt / 0.25f * 0.6f);
        }
        const float length = std::max(restLength(i) * m_out[ti] * (1.0f + stretch), 0.01f);
        const float link = length / static_cast<float>(kPoints - 1);
        Foot& foot = m_feet[ti];
        glm::vec3* p = &m_points[ti * kPoints];
        glm::vec3* previous = &m_previous[ti * kPoints];

        bool pinned = false;
        glm::vec3 tip{0.0f};
        float pull = 14.0f;
        if (ground && !feeler && !lashing && m_out[ti] > 0.6f) {
            const float height = in.bodyRadius + in.lift * m_scale;
            const float out = std::min(length * 0.62f, std::sqrt(std::max(length * length * 0.8f - height * height, 0.05f)));
            const glm::vec3 ideal = in.center + outward * out + lead - up * in.bodyRadius;
            std::optional<glm::vec3> surface;
            if (raycast) {
                surface = raycast(ideal + up * (in.bodyRadius + 0.15f), ideal - up * 0.5f);
            }
            const glm::vec3 wanted = surface ? *surface : ideal;
            auto beginStep = [&](const glm::vec3& from) {
                foot.from = from;
                foot.at = wanted;
                foot.stepping = 0.0f;
                foot.stepTime = glm::clamp(0.17f - 0.012f * speed, 0.085f, 0.17f);
                foot.lift = 0.05f + 0.012f * speed;
            };
            if (!foot.planted) {
                foot.planted = true;
                beginStep(m_started ? p[kPoints - 1] : wanted);
            } else if (foot.stepping < 0.0f) {
                const bool turn = fract(m_gait + (i % 2 == 0 ? 0.0f : 0.5f)) < 0.5f;
                const bool stretched = glm::distance(foot.at, m_center) > length * 0.98f;
                if ((glm::distance(foot.at, wanted) > kStepAt && turn) || stretched) {
                    beginStep(foot.at);
                }
            }
            if (foot.stepping >= 0.0f) {
                const float next = foot.stepping + dt / foot.stepTime;
                foot.at = glm::mix(foot.at, wanted, 0.5f);
                const float s = std::min(next, 1.0f);
                tip = glm::mix(foot.from, foot.at, smooth01(s)) + up * (std::sin(s * glm::pi<float>()) * foot.lift);
                if (next >= 1.0f) {
                    foot.stepping = -1.0f;
                    ++m_steps;
                } else {
                    foot.stepping = next;
                }
            } else {
                tip = foot.at;
            }
            pinned = true;
        } else {
            foot.planted = false;
            foot.stepping = -1.0f;
            const glm::vec3 sway = (a * std::sin(m_time * 2.3f + fi * 1.9f) + b * std::cos(m_time * 1.9f + fi * 1.7f) +
                                    up * std::sin(m_time * 2.9f + fi)) * (0.14f * length);
            if (in.mode == Mode::Reveal) {
                tip = root;
            } else if (lashing && !blow) {
                tip = m_center - toTarget * (length * 0.35f) + up * (length * 0.6f) + outward * (length * 0.2f) + sway;
                pull = 13.0f;
            } else if (blow) {
                const glm::vec3 side = normalizeOr(glm::cross(toTarget, up), a);
                tip = m_center + toTarget * std::min(targetDistance, length) + side * (std::sin(m_time * 30.0f + fi * 1.3f) * 0.12f);
                pull = 32.0f;
            } else if (tumbling) {
                const float flail = std::sin(m_time * 7.0f + fi * 2.1f + in.seed);
                tip = m_center + outward * (length * 0.8f) + up * (length * 0.3f * flail) + sway * 2.2f;
                pull = 5.0f;
            } else if (air) {
                tip = m_center - flying * (length * 0.75f) + outward * (length * 0.35f) + sway;
                pull = 10.0f;
            } else {
                tip = m_center + up * (length * 0.55f) + outward * (length * 0.45f) + lead * 1.5f + sway * 1.5f;
                pull = 8.0f;
            }
        }

        if (!pinned && in.mode != Mode::Reveal) {
            glm::vec3& want = m_wantTip[ti];
            if (!m_free[ti] || !m_started) {
                want = (m_started ? p[kPoints - 1] : tip) - m_center;
            }
            want += (tip - m_center - want) * std::min(1.0f, dt * (blow ? 30.0f : 10.0f));
            tip = m_center + want;
        }
        m_free[ti] = !pinned && in.mode != Mode::Reveal;

        if (!m_started) {
            for (int k = 0; k < kPoints; ++k) {
                p[k] = previous[k] = glm::mix(root, tip, static_cast<float>(k) / static_cast<float>(kPoints - 1));
            }
        }
        if (in.mode == Mode::Reveal) {
            const float unwound = m_out[ti];
            const float spin = (m_time * 15.0f * in.violence + in.seed) * (i % 3 == 0 ? -0.6f : 1.0f);
            const float thrash = std::sin(m_time * 19.0f + fi * 2.3f);
            for (int k = 1; k < kPoints; ++k) {
                const float along = static_cast<float>(k) / static_cast<float>(kPoints - 1);
                const float wound = (1.0f - unwound) * along * 13.0f + along * 0.9f * in.violence;
                const float theta = angle + spin - wound;
                const glm::vec3 round = a * std::cos(theta) + b * std::sin(theta);
                const float reach = 0.08f * m_scale + along * std::min(length, 1.1f) * (0.2f + 0.8f * unwound);
                const float lift = (0.3f + (0.25f + 0.75f * thrash * std::cos(along * 2.5f - m_time * 9.0f)) * in.violence) * along * length * unwound +
                                   (1.0f - unwound) * along * 0.12f;
                const glm::vec3 jolt = (a * std::sin(m_time * 43.0f + fi * 3.1f) + b * std::cos(m_time * 37.0f + fi * 1.9f)) * (0.1f * in.violence * along * unwound);
                previous[k] = p[k];
                p[k] = glm::mix(p[k], m_center + round * reach + up * std::max(lift, -0.2f * along) + jolt, m_posed);
            }
            p[0] = root;
            for (int k = 1; k < kPoints; ++k) {
                p[k] = p[k - 1] + normalizeOr(p[k] - p[k - 1], outward) * link;
            }
            continue;
        }

        for (int k = 1; k < kPoints; ++k) {
            glm::vec3 step = (p[k] - previous[k]) * damping;
            const float moved = glm::length(step);
            if (moved > 0.5f) {
                step *= 0.5f / moved;
            }
            previous[k] = p[k];
            p[k] += step;
            if (!pinned) {
                const float along = static_cast<float>(k) / static_cast<float>(kPoints - 1);
                const float phase = m_time * 6.0f - static_cast<float>(k) * 0.9f + fi * 1.3f;
                p[k] += (a * std::sin(phase) + b * std::cos(phase * 0.8f)) * (12.0f * along * length * dt * dt);
            }
        }

        const glm::vec3 end = pinned ? tip : p[kPoints - 1] + (tip - p[kPoints - 1]) * std::min(1.0f, pull * dt);
        const glm::vec3 bow = up * (0.02f * length);
        for (int iteration = 0; iteration < (pinned ? 4 : 2); ++iteration) {
            if (pinned) {
                for (int k = 1; k < kPoints - 1; ++k) {
                    p[k] += bow;
                }
            }
            p[kPoints - 1] = end;
            for (int k = kPoints - 2; k >= 0; --k) {
                p[k] = p[k + 1] + normalizeOr(p[k] - p[k + 1], up) * link;
            }
            p[0] = root;
            for (int k = 1; k < kPoints; ++k) {
                p[k] = p[k - 1] + normalizeOr(p[k] - p[k - 1], outward) * link;
            }
        }
    }
    m_started = true;
}

}
