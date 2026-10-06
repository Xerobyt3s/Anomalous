#include "app/play_view.h"
#include "engine/assets/asset_path.h"
#include "engine/render/post_process.h"
#include "engine/render/primitives.h"
#include "game/fx/material_shapes.h"
#include "game/fx/necromite_fx.h"
#include "game/player/player_hit.h"

#include <glad/glad.h>
#include <imgui.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cmath>
#include <string>
#include <variant>

namespace anom {

using namespace ::ghost::game;
namespace engine = ::ghost::engine;

namespace {
constexpr float kTableTop = 0.94f;
constexpr glm::vec3 kInspectPosition{0.0f, kTableTop + 0.053f, 0.0f};
constexpr std::size_t kMaxMarks = 256;
constexpr int kStartingPouch = 12;
constexpr glm::vec3 kBenchPosition{2.3f, 0.9f, 1.4f};
constexpr float kRadialHoldTime = 0.18f;
constexpr glm::vec3 kSpawnPosition{0.0f, 0.0f, 2.5f};
constexpr int kArenaRounds = 30;
constexpr float kOtherGunScale = 1.2f;
constexpr glm::vec3 kOtherHandOnGrip{0.0f, -0.022f, -0.02f};
constexpr float kInfernoGroundSpan = 6.0f;
constexpr float kBurnRoundsPerSecond = 0.8f;

constexpr std::uint32_t kVolumeSource = 0x40000000u;
constexpr std::uint32_t kBreathSource = 0x80000000u;
constexpr std::uint32_t kHeadHit = 0x100u;
constexpr float kBreathRoundsPerSecond = 5.0f;
constexpr float kWispMistScale = 0.8f;
constexpr float kWispDisperseTime = 1.4f;
constexpr float kBlinkClearance = 0.4f;
constexpr float kFlickThreshold = 30.0f;
constexpr float kFlickMax = 120.0f;
constexpr float kPickupRadius = 1.3f;
constexpr float kPickupTime = 0.35f;
constexpr float kRoundRadius = 0.0061f;
constexpr std::size_t kMaxDebris = 60;
constexpr float kDebrisLifetime = 30.0f;
constexpr float kDebrisViewmodelTime = 0.45f;
constexpr std::size_t kMaxPuffs = 400;

void setSurface(const engine::Shader& shader, const glm::vec3& color, float metallic, float roughness,
                const glm::vec3& emissive = glm::vec3(0.0f)) {
    shader.set("uBaseColor", color);
    shader.set("uMetallic", metallic);
    shader.set("uRoughness", roughness);
    shader.set("uHighlight", 0.0f);
    shader.set("uEmissive", emissive);
}

void drawWithModel(const engine::Shader& shader, const engine::Mesh& mesh, const glm::mat4& model) {
    shader.set("uModel", model);
    shader.set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    mesh.draw();
}

constexpr int kShadowModelSlot = 0;
constexpr int kShadowLightSlot = 4;
constexpr float kScorchDepth = 0.35f;
constexpr float kHoleSize = 0.022f;
constexpr float kHoleDepth = 0.06f;

glm::mat4 basisFacing(const glm::vec3& forward) {
    const glm::vec3 helper = std::abs(forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(helper, forward));
    const glm::vec3 y = glm::cross(forward, x);
    return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(forward, 0.0f), glm::vec4(0, 0, 0, 1));
}

glm::mat4 basisFacing(const glm::vec3& forward, const glm::vec3& upHint) {
    glm::vec3 y = upHint - forward * glm::dot(upHint, forward);
    if (glm::length(y) < 1e-3f) {
        return basisFacing(forward);
    }
    y = glm::normalize(y);
    const glm::vec3 x = glm::cross(y, forward);
    return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(forward, 0.0f), glm::vec4(0, 0, 0, 1));
}

glm::vec3 impactColor(Surface surface) {
    return surface == Surface::Steel ? glm::vec3(1.0f, 0.6f, 0.25f) : glm::vec3(0.45f, 0.40f, 0.34f);
}

glm::vec3 around(const glm::mat3& basis, float angle, float lift = 0.0f) {
    return basis[0] * std::cos(angle) + basis[1] * lift + basis[2] * std::sin(angle);
}

}

void PlayView::spawnEmber(const glm::vec3& at, float strength) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float angle = unit(m_fxRng) * glm::two_pi<float>();
    const glm::mat3 basis = tornadoBasis(m_play.upAt(at));
    const glm::vec3 up = basis[1];
    const glm::vec3 out = around(basis, angle);
    Spark s;
    s.up = up;
    s.position = at + out * (0.03f + 0.04f * unit(m_fxRng)) + up * 0.04f;
    s.velocity = glm::cross(up, out) * (0.12f * strength) + out * (0.05f * strength) +
                 up * ((0.25f + 0.3f * unit(m_fxRng)) * strength);
    s.color = glm::vec3(1.0f, 0.62f, 0.2f);
    s.coolTo = glm::vec3(0.7f, 0.1f, 0.02f);
    s.size = 0.012f + 0.008f * unit(m_fxRng);
    s.life = 0.8f + 0.7f * unit(m_fxRng);
    s.gravity = -0.02f;
    m_sparks.push_back(s);
}
float PlayView::absorbFlare(const Absorb& a) {
    const float t = a.age / a.life;
    return t < 0.12f ? glm::smoothstep(0.0f, 0.12f, t) : 1.0f - glm::smoothstep(0.12f, 0.7f, t);
}
void PlayView::spawnSparks(const glm::vec3& at, int count, float speed, float upBias, const glm::vec3& color) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::mat3 basis = tornadoBasis(m_play.upAt(at));
    for (int i = 0; i < count; ++i) {
        const glm::vec3 dir = glm::normalize(basis * (glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng) * upBias, unit(m_fxRng) - 0.5f) +
                                                      glm::vec3(0.0f, 0.15f, 0.0f)));
        Spark s;
        s.up = basis[1];
        s.position = at;
        s.velocity = dir * speed * (0.35f + 0.65f * unit(m_fxRng));
        const float heat = unit(m_fxRng);
        s.color = color.x < 0.0f ? glm::mix(glm::vec3(1.0f, 0.35f, 0.08f), glm::vec3(1.0f, 0.85f, 0.45f), heat)
                                 : glm::mix(color, glm::vec3(1.0f), heat * 0.7f);
        s.coolTo = color.x < 0.0f ? glm::vec3(0.8f, 0.12f, 0.02f) : color * 0.4f;
        s.life = 1.2f + unit(m_fxRng) * 1.8f;
        s.size = 0.025f + unit(m_fxRng) * 0.035f;
        s.gravity = unit(m_fxRng) < 0.3f ? -0.35f : 1.0f;
        m_sparks.push_back(s);
    }
    constexpr std::size_t kMaxSparks = 1500;
    if (m_sparks.size() > kMaxSparks) {
        m_sparks.erase(m_sparks.begin(), m_sparks.begin() + static_cast<long>(m_sparks.size() - kMaxSparks));
    }
}
void PlayView::startInferno(const glm::vec3& point, const glm::vec3& normal, float scale, bool scorch) {
    const InfernoTuning& t = m_inferno;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::vec3 center = point + normal * 0.05f;

    ImpactFlash flash{center + normal * 0.3f, Surface::Steel, 0.0f, unit(m_fxRng) * 6.28f};
    flash.color = glm::vec3(1.0f, 0.82f, 0.5f);
    flash.sizeScale = t.flashSize / 0.3f * scale;
    flash.duration = 0.3f;
    flash.spikes = 1.0f;
    m_impactFlashes.push_back(flash);
    m_lightSpikes.push_back({center + normal * 0.6f, glm::vec3(1.0f, 0.6f, 0.25f) * t.flashLight * scale, 0.0f,
                             t.lightTime});

    const glm::mat4 basis = basisFacing(normal);
    const glm::vec3 u = glm::vec3(basis[0]);
    const glm::vec3 v = glm::vec3(basis[1]);
    for (int i = 0; i < 32; ++i) {
        const float a = glm::two_pi<float>() * (static_cast<float>(i) + unit(m_fxRng) * 0.5f) / 32.0f;
        const glm::vec3 out = u * std::cos(a) + v * std::sin(a);
        m_fire.emit(center + normal * 0.1f, out * (7.0f + unit(m_fxRng) * 4.0f) * scale + normal * 0.6f,
                    0.35f * scale, 0.28f + unit(m_fxRng) * 0.12f, 1.0f, 4.5f, 0.7f);
        if (i % 2 == 0) {
            spawnPuff(center + normal * 0.15f, out * (5.0f + unit(m_fxRng) * 3.0f) * scale + normal * 0.4f,
                      glm::vec3(0.35f, 0.3f, 0.26f), 1.4f + unit(m_fxRng), 0.2f, 1.1f * scale, 0.4f);
        }
    }
    m_rings.push_back({center, normal, t.shockRadius * scale, t.shockTime, 0.0f, true, unit(m_fxRng) * 10.0f});

    spawnSparks(center + normal * 0.2f, static_cast<int>(static_cast<float>(t.embers) * scale), 9.0f, 2.5f);

    const glm::vec3 eye = m_lastMuzzleWorld;
    const float proximity = std::clamp(1.0f - glm::distance(eye, center) / t.shakeRange, 0.0f, 1.0f);
    m_blastShake = std::max(m_blastShake, t.shakeDeg * proximity * scale);
    m_fovPunch.velocity.x += 45.0f * proximity * scale;

    if (scorch) {
        m_scorches.push_back({point + normal * 0.008f, normal, t.scorchRadius * scale, 0.0f, unit(m_fxRng) * 10.0f});
    }

    InfernoBlast blast;
    blast.center = center;
    blast.normal = normal;
    blast.scale = scale;
    blast.duration = m_ammo.elements[m_ammo.element("inferno")].volumeLifetime;
    blast.seed = unit(m_fxRng) * 10.0f;
    m_blasts.push_back(blast);
}
void PlayView::updateElementFx(float dt, float alpha) {
    m_fxTime += dt;
    const ElementId fire = m_ammo.element("fire");
    const ElementId inferno = m_ammo.element("inferno");
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (const ElementVolume& v : m_volumes.all()) {
        if (v.element != fire && v.element != inferno) {
            continue;
        }
        const float life = std::min(1.0f, v.remaining01() * 3.0f);

        const bool big = v.element == inferno;
        m_fire.feed(v.center + v.normal * (v.radius * 0.2f), v.radius * (0.4f + 0.6f * life) * (big ? 0.3f : 0.6f),
                    (big ? 0.5f : 1.0f) * life, dt);
    }

    for (const ShownDrop& drop : shownDrops()) {
        if (m_ammo.materials[drop.material].look != MaterialDef::Look::SmallFire) {
            continue;
        }
        m_fire.feed(drop.position + m_play.upAt(drop.position) * 0.03f, 0.07f * drop.scale, 0.55f * drop.scale, dt);
        if (unit(m_fxRng) < dt * 4.0f) {
            spawnEmber(drop.position, 1.0f);
        }
    }
    for (const Absorb& a : m_absorbs) {
        if (a.look != MaterialDef::Look::SmallFire) {
            continue;
        }
        const float flare = absorbFlare(a);
        const float burn = 1.0f - glm::smoothstep(0.45f, 1.0f, a.age / a.life);
        m_fire.feed(a.from + m_play.upAt(a.from) * 0.03f, 0.07f * (1.0f + 1.6f * flare) * std::max(burn, 0.2f),
                    (0.55f + 1.8f * flare) * burn, dt);
        if (unit(m_fxRng) < dt * 30.0f * flare) {
            spawnEmber(a.from, 1.0f + 2.0f * flare);
        }
    }

    for (InfernoBlast& b : m_blasts) {
        b.age += dt;

        for (const ElementVolume& v : m_volumes.all()) {
            if (m_ammo.elements[v.element].vortex.radius <= 0.0f || glm::distance(v.center, b.center) > 2.5f) {
                continue;
            }
            b.normal = v.normal;
            b.center += (v.center - b.normal * 0.03f - b.center) * std::min(1.0f, 14.0f * dt);
            if (v.age + 0.5f < b.age) {
                b.age = std::max(v.age, m_inferno.touchdownTime);
            }
            break;
        }

        b.groundTimer -= dt;
        if (b.groundTimer <= 0.0f || glm::distance(b.groundAt, b.center) > 0.3f) {
            b.groundTimer = 0.1f;
            const glm::mat3 basis = tornadoBasis(b.normal);
            b.groundAt = b.center;
            b.groundBase = glm::dot(b.center, basis[1]);
            const float span = kInfernoGroundSpan * b.scale;
            for (int z = 0; z < kTornadoGroundGrid; ++z) {
                for (int x = 0; x < kTornadoGroundGrid; ++x) {
                    const glm::vec2 offset = (glm::vec2(x, z) / static_cast<float>(kTornadoGroundGrid - 1) - 0.5f) * span;
                    const glm::vec3 above = b.center + basis * glm::vec3(offset.x, 1.0f, offset.y);
                    const auto hit = m_physics.raycast(above, above - basis[1] * 5.0f);
                    b.ground[static_cast<std::size_t>(z * kTornadoGroundGrid + x)] =
                        hit ? glm::dot(hit->point - b.center, basis[1]) : -1e3f;
                }
            }
        }
        const TornadoInstance t = tornadoOf(b);
        const glm::mat3 axes = tornadoBasis(t.axis);
        const glm::vec3 axis = axes[1];
        const float spin = m_inferno.style.spin;
        const float reach = std::min(t.grow, 1.0f) * t.height;
        const float alive = t.intensity * (1.0f - t.rope);

        if (unit(m_fxRng) < dt * 70.0f * alive) {
            const float h = unit(m_fxRng);
            const float a = unit(m_fxRng) * glm::two_pi<float>();
            const float r = glm::mix(t.baseRadius, t.topRadius, std::pow(h, 1.6f)) * t.thick * (0.8f + 0.5f * unit(m_fxRng));
            const glm::vec3 radial = around(axes, a);
            Spark s;
            s.up = axis;
            s.position = t.base + radial * r + axis * (h * reach);
            s.velocity = axis * (2.0f + 2.0f * unit(m_fxRng));
            s.color = glm::mix(glm::vec3(1.0f, 0.35f, 0.08f), glm::vec3(1.0f, 0.85f, 0.45f), unit(m_fxRng));
            s.life = 0.9f + unit(m_fxRng) * 1.2f;
            s.size = 0.025f + unit(m_fxRng) * 0.035f;
            s.gravity = -0.2f;
            s.orbitCenter = t.base;
            s.orbit = spin;
            m_sparks.push_back(s);
        }
        if (unit(m_fxRng) < dt * 28.0f * alive) {
            const float a = unit(m_fxRng) * glm::two_pi<float>();
            const glm::vec3 radial = around(axes, a);
            const float distance = (2.0f + 1.4f * unit(m_fxRng)) * b.scale;
            spawnPuff(t.base + radial * distance + axis * 0.1f, glm::vec3(0.0f),
                      glm::vec3(0.3f, 0.24f, 0.19f), 0.7f + 0.4f * unit(m_fxRng), 0.15f, 0.55f, 0.3f);
        }
        b.smokeTimer -= dt;
        if (b.age > m_inferno.touchdownTime && b.smokeTimer <= 0.0f && t.intensity > 0.05f) {
            b.smokeTimer = 0.1f;
            const float radius = t.topRadius * t.thick;
            const glm::vec3 jitter = axes[0] * (unit(m_fxRng) - 0.5f) + axes[2] * (unit(m_fxRng) - 0.5f);
            spawnPuff(t.base + axis * (reach * 1.02f) + jitter * radius * 1.6f,
                      axis * (1.3f + unit(m_fxRng) * 0.7f) + jitter * 0.8f,
                      glm::vec3(0.10f, 0.08f, 0.065f), 3.0f + unit(m_fxRng) * 1.5f, 0.6f * b.scale,
                      (1.8f + unit(m_fxRng)) * b.scale, 0.28f * alive);
        }
    }
    std::erase_if(m_blasts, [](const InfernoBlast& b) { return b.age >= b.duration; });

    for (ActiveJet& jet : m_jets) {
        jet.age += dt;
        m_fire.jet(jet.origin, jet.direction, jet.length / 0.35f, 0.45f, dt);
        m_fire.jet(jet.origin, jet.direction, jet.length / 0.5f, 0.3f, dt);
    }
    std::erase_if(m_jets, [](const ActiveJet& j) { return j.age > 0.5f; });

    for (Spark& s : m_sparks) {
        s.age += dt;
        if (glm::dot(s.up, s.up) < 1e-6f) {
            s.up = m_play.upAt(s.position);
        }
        const glm::vec3 up = s.up;
        if (s.orbit > 0.0f) {
            glm::vec3 radial = s.position - s.orbitCenter;
            radial -= up * glm::dot(radial, up);
            const float d = glm::length(radial);
            if (d > 1e-3f) {
                radial /= d;
                const glm::vec3 swirl = glm::cross(up, radial) * (s.orbit * d) + radial * 0.5f;
                s.velocity = up * glm::dot(s.velocity, up) + swirl;
            }
            if (s.age > s.life * 0.6f) {
                s.orbit = 0.0f;
                s.gravity = 0.6f;
            }
        }
        s.velocity -= up * (9.81f * s.gravity * dt);
        s.velocity *= std::exp(-0.8f * dt);
        s.position += s.velocity * dt;
    }
    std::erase_if(m_sparks, [](const Spark& s) { return s.age >= s.life; });
    for (Ring& r : m_rings) {
        r.age += dt;
    }
    std::erase_if(m_rings, [](const Ring& r) { return r.age >= r.duration; });
    for (Scorch& s : m_scorches) {
        s.age += dt;
    }
    std::erase_if(m_scorches, [](const Scorch& s) { return s.age > 90.0f; });
    for (LightSpike& l : m_lightSpikes) {
        l.age += dt;
    }
    std::erase_if(m_lightSpikes, [](const LightSpike& l) { return l.age >= l.duration; });
    m_blastShake *= std::exp(-7.0f * dt);

    const glm::vec3 windAtPlayer = airVelocity(m_vortices, m_world.position + glm::vec3(0.0f, 0.9f, 0.0f));
    if (true) {
        m_blastShake = std::max(m_blastShake, 0.07f * glm::length(windAtPlayer));
    }

    for (BulletWake& wake : m_wakes) {
        wake.alive = false;
    }
    for (const Projectile& p : m_ballistics.projectiles()) {
        const ElementDef& def = m_ammo.elements[p.round.element];
        if ((def.trail != TrailKind::None && def.trail != TrailKind::Ribbon) || p.ethereal) {
            continue;
        }
        auto it = std::find_if(m_wakes.begin(), m_wakes.end(), [&](const BulletWake& w) { return w.id == p.id; });
        if (it == m_wakes.end()) {
            const bool fresh = m_play.simTime() - m_lastShotTime < 0.25;
            m_wakes.push_back({p.id, fresh ? m_lastMuzzleWorld : p.previousPosition, p.position, 0.0f, true, p.ricochets});
            it = m_wakes.end() - 1;
        }
        if (p.ricochets != it->ricochets) {
            it->from = p.previousPosition;
            it->ricochets = p.ricochets;
        }
        it->to = glm::mix(p.previousPosition, p.position, alpha);
        it->alive = true;
    }
    for (BulletWake& wake : m_wakes) {
        if (!wake.alive) {
            wake.fade += dt;
        }
    }
    std::erase_if(m_wakes, [](const BulletWake& w) { return w.fade >= 0.12f; });

    for (const Projectile& p : m_ballistics.projectiles()) {
        const ElementDef& def = m_ammo.elements[p.round.element];
        if (def.trail == TrailKind::None || (p.ethereal && p.owner != m_local)) {
            continue;
        }
        const glm::vec3 now = glm::mix(p.previousPosition, p.position, alpha);
        auto [it, inserted] = m_trailHeads.try_emplace(p.id, now);
        if (inserted && m_play.simTime() - m_lastShotTime < 0.25) {
            it->second = m_lastMuzzleWorld;
        }
        const glm::vec3 from = it->second;
        if (def.trail == TrailKind::Haze) {
            updateComet(p, from, now, dt);
            it->second = now;
            continue;
        }
        const float length = glm::distance(from, now);
        if (length < 0.05f) {
            continue;
        }
        const glm::vec3 dir = (now - from) / length;
        const glm::mat4 basis = basisFacing(dir);
        const glm::vec3 side = glm::vec3(basis[0]);
        const glm::vec3 side2 = glm::vec3(basis[1]);
        if (def.trail == TrailKind::Ribbon) {
            m_ribbons.push_back({from, now, 0.0f});
        }
        if (def.trail == TrailKind::Whirl || def.trail == TrailKind::FlamingWhirl) {
            addTrail(from, now, p.round.element);
        }
        if (def.trail == TrailKind::Whirl) {
            for (float s = 0.0f; s < length; s += 0.35f) {
                const glm::vec3 at = glm::mix(from, now, s / length);
                const float a = glm::dot(at, glm::vec3(3.1f, 2.3f, 2.7f)) + static_cast<float>(p.id);
                const glm::vec3 out = side * std::cos(a) + side2 * std::sin(a);
                spawnPuff(at + out * 0.08f, out * (0.7f + unit(m_fxRng) * 0.5f) + dir * 0.6f,
                          glm::vec3(0.78f, 0.88f, 0.95f), 0.9f + unit(m_fxRng) * 0.5f, 0.05f, 0.45f, 0.22f);
            }
        }
        if (def.trail == TrailKind::Embers) {
            for (float s = 0.0f; s < length; s += 0.45f) {
                m_fire.burst(glm::mix(from, now, s / length), 0.035f, 1, 0.25f);
            }
        }
        if (def.trail == TrailKind::FlamingWhirl) {
            for (float s = 0.0f; s < length; s += 0.2f) {
                const glm::vec3 at = glm::mix(from, now, s / length);
                const glm::vec3 jitter = side * (unit(m_fxRng) - 0.5f) + side2 * (unit(m_fxRng) - 0.5f);
                m_fire.emit(at, -dir * 2.5f + jitter * 1.2f + m_play.upAt(at) * 0.6f, 0.18f,
                            0.45f + unit(m_fxRng) * 0.25f, 1.0f, 2.5f, 0.5f);
            }
            if (unit(m_fxRng) < length * 0.8f) {
                spawnSparks(now, 1, 1.5f, 1.0f);
            }
        }
        it->second = now;
    }

    std::erase_if(m_trailHeads, [&](const auto& entry) {
        return std::none_of(m_ballistics.projectiles().begin(), m_ballistics.projectiles().end(),
                            [&](const Projectile& p) { return p.id == entry.first; });
    });

    for (HazeComet& c : m_comets) {
        if (!c.alive) {
            c.fade -= dt / 0.3f;
            c.tail *= std::exp(-dt / 0.12f);
        }
        c.alive = false;
    }
    std::erase_if(m_comets, [](const HazeComet& c) { return c.fade <= 0.0f; });
    for (Ripple& r : m_ripples) {
        r.age += dt;
    }
    std::erase_if(m_ripples, [](const Ripple& r) { return r.age >= r.duration; });
    for (TrailSegment& t : m_trails) {
        t.age += dt;
    }
    std::erase_if(m_trails, [](const TrailSegment& t) { return t.age > t.life; });
    for (RibbonPiece& r : m_ribbons) {
        r.age += dt;
    }
    std::erase_if(m_ribbons, [](const RibbonPiece& r) { return r.age > kRibbonLife; });

    m_fire.update(dt);
    updateSelfFx(dt);
    updateFogFx(dt);
    updateGhostFx(dt);
    updateRevealFx(dt);
    updateBreathFx(dt);
    updateLightning(dt);
}
void PlayView::updateComet(const Projectile& p, const glm::vec3& from, const glm::vec3& now, float dt) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    auto it = std::find_if(m_comets.begin(), m_comets.end(), [&](const HazeComet& c) { return c.id == p.id; });
    if (it == m_comets.end()) {
        m_comets.push_back({p.id, now, glm::vec3(0.0f, 0.0f, -1.0f), 0.0f, 0.0f, 1.0f, true, unit(m_fxRng) * 40.0f});
        it = m_comets.end() - 1;
    }
    HazeComet& comet = *it;
    const float step = glm::distance(from, now);
    if (step > 1e-4f) {
        comet.direction = (now - from) / step;
    }
    comet.head = now;
    comet.alive = true;
    comet.travelled += step;

    comet.tail = std::min(comet.tail * std::exp(-dt / std::max(m_hazeTuning.tailLinger, 0.01f)) + step, 6.0f);

    const glm::vec3 cold{0.55f, 0.7f, 1.0f};
    comet.moteBudget += step * 3.0f;
    while (comet.moteBudget >= 1.0f) {
        comet.moteBudget -= 1.0f;
        spawnSparks(glm::mix(from, now, unit(m_fxRng)), 1, 0.12f, 1.0f, cold);
        Spark& mote = m_sparks.back();
        mote.gravity = 0.02f;
        mote.life = 0.5f + unit(m_fxRng) * 0.9f;
        mote.size = 0.006f + unit(m_fxRng) * 0.007f;
        mote.color *= 0.45f;
    }

    if (step > 1e-3f) {
        if (const auto wall = m_physics.raycast(from, now)) {
            m_ripples.push_back({wall->point, 0.0f, 0.45f, 0.5f, unit(m_fxRng) * 10.0f});
            spawnSparks(wall->point + wall->normal * 0.02f, 4, 0.5f, 1.0f, cold);
        }
    }
}
void PlayView::drawComets(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    if (m_comets.empty()) {
        return;
    }
    m_comet.begin(viewProj, cameraPos, m_fxTime);
    for (const HazeComet& c : m_comets) {
        if (!inWindow(c.head)) {
            continue;
        }
        m_comet.draw(c.head, c.direction, m_hazeTuning.headRadius, c.tail, std::clamp(c.fade, 0.0f, 1.0f),
                     m_hazeTuning.brightness, c.seed);
    }
    m_comet.end();
}
void PlayView::addTrail(const glm::vec3& from, const glm::vec3& to, ElementId element) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float life = element == m_ammo.element("wind") ? 1.2f : 0.9f;
    const float length = glm::distance(from, to);
    const int pieces = std::max(1, static_cast<int>(std::ceil(length / 1.0f)));
    for (int i = 0; i < pieces; ++i) {
        const float a = static_cast<float>(i) / static_cast<float>(pieces);
        const float b = static_cast<float>(i + 1) / static_cast<float>(pieces);
        m_trails.push_back(
            {glm::mix(from, to, a), glm::mix(from, to, b), element, 0.0f, unit(m_fxRng) * 10.0f, life});
    }
}
void PlayView::drawElementFx(const glm::mat4& viewProj, const glm::mat4& invView) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_fire.draw(viewProj, invView, m_fxTime, glm::vec3(1.0f), m_window.nearD, m_window.farD);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    const ElementId inferno = m_ammo.element("inferno");
    m_vortex.begin(viewProj, m_fxTime);
    const glm::vec3 eye = glm::vec3(invView[3]);

    for (const TrailSegment& t : m_trails) {
        if (!inWindow((t.from + t.to) * 0.5f)) {
            continue;
        }
        const float age01 = t.age / t.life;
        const bool burning = t.element == inferno;

        const float nearest = std::min(glm::distance(eye, t.from), glm::distance(eye, t.to));
        const float nearFade = glm::smoothstep(1.2f, 4.0f, nearest);
        Vortex::Style style;
        style.radius = (burning ? 0.07f : 0.06f) + (burning ? 0.28f : 0.3f) * std::sqrt(age01);
        style.intensity = (burning ? 0.95f : 0.9f) * std::pow(1.0f - age01, 1.2f) * nearFade;
        style.spin = burning ? 7.0f : 11.0f;
        style.fire = burning;
        style.seed = t.seed;
        m_vortex.drawAlong(t.from, t.to, style);
        if (burning && age01 < 0.2f) {
            Vortex::Style core = style;
            core.radius = 0.025f;
            core.intensity = 2.2f * (1.0f - age01 / 0.2f) * nearFade;
            core.spin = 3.0f;
            m_vortex.drawAlong(t.from, t.to, core);
        }
    }

    for (const BreathFx& b : m_breathFx) {
        const float in = std::clamp(b.age / 0.15f, 0.0f, 1.0f);
        const float grow = (1.0f - (1.0f - in) * (1.0f - in) * (1.0f - in)) *
                           std::clamp((b.duration - b.age) / 0.2f, 0.0f, 1.0f);
        if (grow <= 0.02f || !inWindow(b.origin)) {
            continue;
        }
        const bool play = true && b.mine;
        const glm::vec3 origin = play ? m_lastMuzzleWorld : b.origin;

        const glm::vec3 forward = play ? m_aimDirection : b.direction;
        const float reach = b.range * 0.95f * grow;
        const float flick = 0.9f + 0.2f * std::sin(m_fxTime * 9.1f) + 0.13f * std::sin(m_fxTime * 15.7f);
        Vortex::Style style;
        style.radius = reach * std::tan(b.halfAngle);
        style.funnel = 0.8f;
        style.spin = 2.5f;
        style.intensity = 0.09f * flick * grow;
        style.ghost = true;
        style.seed = b.range;
        style.fadeEnds = true;
        m_vortex.drawAlong(origin, origin + forward * reach, style);
    }

    for (const Ring& r : m_rings) {
        if (!inWindow(r.center)) {
            continue;
        }
        const float t = r.age / r.duration;
        const float reach = 1.0f - (1.0f - t) * (1.0f - t);
        Vortex::Style style;
        style.radius = glm::mix(0.3f, r.maxRadius, reach);
        style.spin = 14.0f;
        style.intensity = (r.fire ? 1.7f : 1.1f) * (1.0f - t) * (1.0f - t);
        style.fire = r.fire;
        style.seed = r.seed;
        style.fadeEnds = true;
        const float height = glm::mix(0.5f, 0.12f, t) * (r.fire ? 1.0f : 0.7f);
        m_vortex.drawAlong(r.center - r.normal * 0.02f, r.center + r.normal * height, style);
    }

    m_vortex.end();
    drawLightning(viewProj, eye);
    drawRibbons(viewProj, eye);
    drawComets(viewProj, eye);
    drawWhirlwind(viewProj, eye, -glm::vec3(invView[2]));
}
void PlayView::addBolt(ActiveBolt bolt, const BoltParams& params) {
    std::uniform_int_distribution<std::uint32_t> seeds;
    bolt.seed = seeds(m_fxRng);
    bolt.bolt = buildBolt(bolt.from, bolt.to, bolt.seed, params);
    m_bolts.push_back(std::move(bolt));
}
void PlayView::updateSelfFx(float dt) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const SelfFxTuning& tune = m_selfFxTuning;
    SelfFx& fx = m_selfFx;
    const ghost::game::PlayerState& s = m_world;
    const bool play = true;
    auto approach = [dt](float value, float target, float rate) {
        return value + (target - value) * (1.0f - std::exp(-rate * dt));
    };

    fx.haste = approach(fx.haste, play ? std::min(1.0f, s.hasteTime / 0.7f) : 0.0f, 9.0f);
    fx.shroud = approach(fx.shroud, play ? std::min(1.0f, s.shroudTime / 0.5f) : 0.0f, 7.0f);
    fx.shroudCast += dt;
    fx.blinkGlow = std::max(0.0f, fx.blinkGlow - dt / std::max(fx.blinkDuration, 0.1f));
    fx.blinkRush *= std::exp(-dt / 0.07f);

    const glm::vec3 feet = s.position;
    const glm::vec3 chest = feet + glm::vec3(0.0f, 1.1f, 0.0f);
    const glm::vec3 flatVelocity{s.velocity.x, 0.0f, s.velocity.z};
    const float speed = glm::length(flatVelocity);

    if (fx.shroud > 0.02f) {
        fx.mistBudget += dt * fx.shroud * tune.mistDensity * 16.0f;
        while (fx.mistBudget >= 1.0f) {
            fx.mistBudget -= 1.0f;
            const float around = unit(m_fxRng) * glm::two_pi<float>();
            const glm::vec3 radial{std::cos(around), 0.0f, std::sin(around)};
            const glm::vec3 tangent{radial.z, 0.0f, -radial.x};
            spawnPuff(feet + radial * (1.1f + 1.4f * unit(m_fxRng)) + glm::vec3(0.0f, 0.1f + 1.3f * unit(m_fxRng), 0.0f),
                      tangent * 0.5f + flatVelocity * 0.6f, glm::vec3(0.36f, 0.5f, 0.54f), 1.2f + unit(m_fxRng) * 0.8f,
                      0.35f, 1.1f, 0.1f);
        }
    }

    const glm::vec3 cold{0.55f, 0.7f, 1.0f};
    if (fx.blinkGlow > 0.01f) {
        const float glow = fx.blinkGlow;
        auto onShell = [&] {
            const float a = unit(m_fxRng) * glm::two_pi<float>();
            const float h = unit(m_fxRng) * 2.0f - 1.0f;
            const float ring = std::sqrt(std::max(0.0f, 1.0f - h * h));
            return chest + glm::vec3(std::cos(a) * ring, h * 0.9f, std::sin(a) * ring) * (0.5f + 0.35f * unit(m_fxRng));
        };
        fx.arcBudget += dt * tune.arcRate * glow * std::sqrt(glow);
        while (fx.arcBudget >= 1.0f) {
            fx.arcBudget -= 1.0f;
            ActiveBolt arc;
            arc.from = onShell();
            if (unit(m_fxRng) < 0.35f) {
                const float a = unit(m_fxRng) * glm::two_pi<float>();
                glm::vec3 ground = feet + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * (0.5f + 1.3f * unit(m_fxRng));
                if (const auto hit = m_physics.raycast(ground + glm::vec3(0.0f, 1.0f, 0.0f), ground - glm::vec3(0.0f, 1.5f, 0.0f))) {
                    ground = hit->point;
                }
                arc.to = ground;
                spawnSparks(ground + glm::vec3(0.0f, 0.03f, 0.0f), 2, 1.5f, 1.5f, cold);
            } else {
                arc.to = onShell();
            }
            const float second = 0.04f + 0.04f * unit(m_fxRng);
            arc.flashTimes = {0.0f, second};
            arc.width = 0.012f;
            arc.power = 0.35f + 0.6f * glow;
            arc.light = 6.0f * glow;
            BoltParams params;
            params.levels = 4;
            params.jaggedness = 0.3f;
            params.branches = 1;
            addBolt(std::move(arc), params);
        }

        fx.gunArcBudget += dt * tune.arcRate * 0.6f * glow;
        while (fx.gunArcBudget >= 1.0f) {
            fx.gunArcBudget -= 1.0f;
            const glm::vec3 anchors[4] = {m_revolver.muzzle(), m_revolver.cylinderGap(),
                                          m_revolver.cylinderGap() + glm::vec3(0.0f, -0.05f, -0.05f),
                                          glm::mix(m_revolver.muzzle(), m_revolver.cylinderGap(), 0.5f)};
            auto jittered = [&](int i) {
                return anchors[i] + glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f) * 0.03f;
            };
            const int a = static_cast<int>(unit(m_fxRng) * 3.999f);
            const int b = (a + 1 + static_cast<int>(unit(m_fxRng) * 2.999f)) % 4;
            BoltParams params;
            params.levels = 3;
            params.jaggedness = 0.35f;
            params.branches = 1;
            params.branchLength = 0.5f;
            GunArc arc;
            std::uniform_int_distribution<std::uint32_t> seeds;
            arc.bolt = buildBolt(jittered(a), jittered(b), seeds(m_fxRng), params);
            arc.life = 0.08f + 0.08f * unit(m_fxRng);
            arc.seed = unit(m_fxRng) * 50.0f;
            m_gunArcs.push_back(std::move(arc));
        }
    }
    for (GunArc& arc : m_gunArcs) {
        arc.age += dt;
    }
    std::erase_if(m_gunArcs, [](const GunArc& a) { return a.age >= a.life; });

    engine::PostProcess::Settings& post = m_post->settings();
    const float rush = fx.haste * tune.rushStrength * std::clamp(speed / 8.0f, 0.12f, 1.0f);
    post.aura = glm::vec3(std::min(1.0f, rush + fx.blinkRush * 1.4f), fx.shroud, fx.blinkGlow * fx.blinkGlow);
    post.rushColor = fx.blinkRush > rush ? glm::vec3(0.6f, 0.75f, 1.0f) : glm::vec3(0.8f, 0.95f, 0.9f);
}
void PlayView::drawWhirlwind(const glm::mat4& viewProj, const glm::vec3& eye, const glm::vec3& forward) {
    if (m_selfFx.haste <= 0.02f || false || m_window.nearD >= 0.0f) {
        return;
    }

    const ghost::game::PlayerState& s = m_world;
    glm::vec3 target{s.velocity.x, 0.0f, s.velocity.z};
    if (glm::length(target) < 0.8f) {
        target = glm::vec3(forward.x, 0.0f, forward.z);
    }
    if (glm::length(target) > 1e-3f) {
        m_whirlAxis = glm::normalize(glm::mix(m_whirlAxis, glm::normalize(target), 1.0f - std::exp(-7.0f * m_frameDt)) +
                                     glm::vec3(0.0f, 0.0f, -1e-4f));
    }
    const glm::vec3 axis = m_whirlAxis;
    const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), axis));
    const glm::vec3 up = glm::cross(axis, side);
    const glm::vec3 centre = eye - glm::vec3(0.0f, 0.35f, 0.0f);
    const float speed = glm::length(glm::vec2(s.velocity.x, s.velocity.z));
    const float strength = m_selfFx.haste * m_selfFxTuning.whirlwind;

    constexpr int kStrands = 16;
    constexpr int kPoints = 22;
    constexpr float kAhead = 6.0f;
    constexpr float kBehind = 2.5f;
    const float turnRate = 4.5f;
    const float flow = 0.45f + 0.05f * speed;

    m_lightning.begin(viewProj, eye);
    for (int r = 0; r < kStrands; ++r) {
        const float phase = static_cast<float>(r) * 2.399963f;
        const float vary = 0.5f + 0.5f * std::sin(phase * 3.1f);
        const float pass = std::fmod(m_fxTime * flow * (0.8f + 0.4f * vary) + static_cast<float>(r) / kStrands, 1.0f);
        const float life = std::sin(pass * glm::pi<float>());
        const float radius = 1.05f + 0.6f * vary;
        const float length = 1.8f + 1.6f * vary;
        const float wrap = 1.6f + 1.2f * vary;
        const float head = glm::mix(kAhead, -kBehind, pass);

        Bolt strand;
        BoltPath path;
        for (int i = 0; i < kPoints; ++i) {
            const float t = static_cast<float>(i) / (kPoints - 1);
            const float angle = phase + m_fxTime * turnRate + t * wrap;
            const glm::vec3 point = centre + axis * (head + t * length) +
                                    (side * std::cos(angle) + up * std::sin(angle)) * radius;
            const glm::vec3 toPoint = glm::normalize(point - eye);
            const float offCentre = 1.0f - glm::dot(toPoint, forward);
            const float atEdge = glm::smoothstep(0.08f, 0.3f, offCentre);
            path.points.push_back(point);
            path.width.push_back(std::sin(t * glm::pi<float>()) * atEdge);
            path.reach.push_back(t);
        }
        strand.paths.push_back(std::move(path));

        BoltStyle style;
        style.width = 0.012f + 0.014f * vary;
        style.haloScale = 6.0f;
        style.color = glm::vec3(0.72f, 0.95f, 0.88f);
        style.intensity = 0.11f * strength * life;
        m_lightning.draw(strand, style);
    }
    m_lightning.end();
}
void PlayView::drawGunArcs(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::mat4& gunToWorld) {
    if (m_gunArcs.empty()) {
        return;
    }
    m_lightning.begin(viewProj, cameraPos);
    for (const GunArc& arc : m_gunArcs) {
        Bolt placed = arc.bolt;
        for (BoltPath& path : placed.paths) {
            for (glm::vec3& p : path.points) {
                p = glm::vec3(gunToWorld * glm::vec4(p, 1.0f));
            }
        }
        BoltStyle style;
        style.width = 0.0022f;
        style.haloScale = 5.0f;
        style.intensity = 0.8f * (1.0f - arc.age / arc.life) * (0.4f + 0.6f * std::sqrt(m_selfFx.blinkGlow));
        style.jitter = 0.002f;
        style.jitterSeed = arc.seed + std::floor(arc.age * 40.0f);
        m_lightning.draw(placed, style);
    }
    m_lightning.end();
}
void PlayView::updateBreathFx(float dt) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::vec3 pale{0.45f, 1.0f, 0.8f};
    for (BreathFx& b : m_breathFx) {
        b.age += dt;
        if (b.age >= b.duration) {
            continue;
        }
        const bool play = true && b.mine;
        const glm::vec3 origin = play ? m_lastMuzzleWorld : b.origin;

        const glm::vec3 forward = play ? m_aimDirection : b.direction;
        const glm::mat4 basis = basisFacing(forward);
        const float spread = std::tan(b.halfAngle);
        const float strength = 1.0f - 0.5f * b.age / b.duration;

        constexpr float kDrag = 2.6f;
        const float speed = b.range * kDrag;
        b.budget += dt * 260.0f;
        while (b.budget >= 1.0f) {
            b.budget -= 1.0f;
            const float around = unit(m_fxRng) * glm::two_pi<float>();
            const float off = spread * std::sqrt(unit(m_fxRng));
            const glm::vec3 dir = glm::normalize(forward + (glm::vec3(basis[0]) * std::cos(around) +
                                                            glm::vec3(basis[1]) * std::sin(around)) * off);
            const float core = 1.0f - off / std::max(spread, 1e-3f);

            m_fire.emit(origin + dir * 0.03f, dir * speed * (0.85f + 0.15f * unit(m_fxRng)),
                        0.5f + 0.3f * unit(m_fxRng), 0.45f + 0.4f * unit(m_fxRng),
                        -(0.65f + 0.35f * core),
                        kDrag, 0.3f);
        }

        if (unit(m_fxRng) < dt * 60.0f) {
            spawnSparks(origin + forward * (0.4f + b.range * 0.5f * unit(m_fxRng)), 1, 3.0f, 0.6f, pale);
            m_sparks.back().gravity = -0.15f;
        }
        const float front = b.range * std::min(1.0f, b.age / (b.duration * 0.5f));
        m_lightSpikes.push_back({origin + forward * (0.4f + front * 0.5f), pale * 4.0f * strength, 0.0f, 0.06f});
    }
    std::erase_if(m_breathFx, [](const BreathFx& b) { return b.age >= b.duration; });
}
void PlayView::updateRevealFx(float dt) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    for (const RevealPulse& pulse : m_reveals) {
        m_revealFxBudget += dt * std::min(pulse.radius * 2.0f + 6.0f, 40.0f);
        while (m_revealFxBudget >= 1.0f) {
            m_revealFxBudget -= 1.0f;
            const float a = unit(m_fxRng) * glm::two_pi<float>();
            spawnSparks(pulse.origin + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * pulse.radius + glm::vec3(0.0f, 0.2f, 0.0f), 1,
                        2.0f, 2.0f);
            m_sparks.back().life *= 0.4f;
        }
    }
    for (RevealShot& shot : m_revealShots) {
        shot.age += dt;
    }
    std::erase_if(m_revealShots, [](const RevealShot& s) { return s.age >= s.duration; });

    const bool active = !m_reveals.empty() || !m_revealShots.empty();
    m_revealDim += ((active ? 0.38f : 0.0f) - m_revealDim) * (1.0f - std::exp(-(active ? 9.0f : 3.0f) * dt));
}
void PlayView::drawRevealMarks(const glm::mat4& viewProj, const glm::mat4& invView, const glm::vec3& cameraPos,
                           const glm::vec3& cameraForward) {
    if (m_revealDim > 0.004f) {
        m_revealFx.dim(m_revealDim);
    }

    for (const RevealPulse& pulse : m_reveals) {
        constexpr int kSegments = 120;
        std::vector<FireRingPoint> points;
        points.reserve(kSegments + 1);
        for (int i = 0; i <= kSegments; ++i) {
            const float a = glm::two_pi<float>() * static_cast<float>(i % kSegments) / kSegments;
            const glm::vec3 out{std::cos(a), 0.0f, std::sin(a)};
            glm::vec3 at = pulse.origin + out * pulse.radius;
            if (const auto ground = m_physics.raycast(at + glm::vec3(0.0f, 2.0f, 0.0f), at - glm::vec3(0.0f, 1.0f, 0.0f))) {
                at = ground->point;
            }
            points.push_back({at, out, glm::two_pi<float>() * static_cast<float>(i) / kSegments * pulse.radius});
        }
        const float spent = pulse.radius / std::max(pulse.range, 1.0f);
        const float intensity = (1.0f - 0.4f * spent) * std::clamp((pulse.range - pulse.radius) / 4.0f, 0.0f, 1.0f) *
                                std::min(1.0f, pulse.radius / 0.6f);
        m_revealFx.drawRing(points, 0.75f - 0.25f * spent, 2.2f, intensity * 1.2f, viewProj, m_fxTime);
    }

    if (m_revealShots.empty()) {
        return;
    }
    const SceneDepthInfo depth{m_sceneDepthCopy, m_post->zNear(), m_post->zFar(),
                               engine::PostProcess::depthSplit()};
    const glm::vec3 ember{1.0f, 0.5f, 0.12f};
    const float step = std::floor(m_fxTime * 16.0f);
    for (const RevealShot& shot : m_revealShots) {
        const float in = std::min(1.0f, shot.age / 0.06f);
        const float fade = std::clamp((shot.duration - shot.age) / 0.5f, 0.0f, 1.0f);
        const float strength = fade * (in + 1.2f * std::max(0.0f, 1.0f - shot.age / 0.3f));

        m_revealFx.beginMask();
        if (shot.body) {
            m_blob.begin(viewProj, cameraPos, m_lightDir, engine::PostProcess::depthSplit(), m_fxTime);
            m_blob.draw(shot.pose, shot.headRadius, shot.color, shot.seed);
            m_blob.end();
        } else {
            m_wispFx.begin(viewProj, cameraPos, cameraForward, depth, 0.0f, true);
            setGhostLook(shot.apparition);
            m_wispFx.draw(shot.heart, shot.cloud, shot.tail, shot.radius, 1.0f, 0.0f, shot.seed, shot.phase, shot.flow);
            m_wispFx.end();
        }
        m_revealFx.endMask();
        m_revealFx.outline(ember, strength, shot.seed, m_fxTime);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDisable(GL_DEPTH_TEST);
        m_flashShader.use();
        const float flick = 0.8f + 0.2f * std::sin(step * 2.3f + shot.seed);
        drawFlashAxes(viewProj, shot.heart + glm::vec3(0.0f, 0.05f, 0.0f), glm::vec3(invView[0]) * 0.5f,
                      glm::vec3(invView[1]) * (1.4f * flick), 0.16f, glm::vec3(1.0f, 0.62f, 0.2f), 1.5f * strength,
                      shot.seed + step, 0.0f, 0.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}
glm::vec3 PlayView::ghostShownAt(const Ghost& ghost) const {
    if (ghost.hitstop <= 0.0f) {
        return ghost.position;
    }
    const float t = std::floor(static_cast<float>(m_fxTime) * 60.0f) + ghost.seed;
    auto noise = [t](float k) { return glm::fract(std::sin(t * k) * 43758.5453f) - 0.5f; };
    return ghost.position + glm::vec3(noise(12.9898f), noise(78.233f), noise(37.719f)) * 0.09f;
}
glm::vec3 PlayView::wispColor(const Ghost& ghost) const {
    if (m_ghosts.def(ghost).invisible) {
        return glm::vec3(0.35f, 1.0f, 0.45f);
    }
    const glm::vec3 calm{0.12f, 0.75f, 1.0f};
    const glm::vec3 angry{0.7f, 1.0f, 1.0f};
    return ghost.state == GhostState::Rush ? glm::mix(calm, angry, std::min(1.0f, ghost.stateTime * 4.0f)) : calm;
}
float PlayView::wispGlow(const Ghost& ghost) const {
    if (m_ghosts.def(ghost).invisible) {
        return 0.0f;
    }
    const float pulse = 0.85f + 0.15f * std::sin(ghost.age * 2.3f + ghost.seed);
    switch (ghost.state) {
    case GhostState::Lure:
        return 1.0f * pulse;
    case GhostState::Rush:
        return 1.8f;
    case GhostState::Flinch:
        return 0.35f + 0.5f * std::abs(std::sin(ghost.age * 40.0f));
    case GhostState::Wander:
    default:
        return 0.55f * pulse;
    }
}
void PlayView::updateAudio(float dt) {
    const ghost::game::PlayerState& s = m_world;
    AudioFrame frame;
    frame.dt = dt;
    frame.listener = m_frame.onFoot ? s.position + glm::vec3(0.0f, m_world.eyeHeight, 0.0f) : m_cameraPos;
    frame.listenerRight = glm::vec3(std::cos(s.yaw), 0.0f, std::sin(s.yaw));
    frame.onFoot = m_frame.onFoot && !m_frame.downed;
    frame.feet = s.position;
    frame.velocity = s.velocity;
    frame.grounded = s.grounded;

    frame.sliding = (s.stance == Stance::Slide || (s.stance == Stance::Crawl && glm::length(glm::vec2(s.velocity.x, s.velocity.z)) > m_playerTuning.crawlSpeed + 0.3f)) && s.grounded;
    frame.crawling = s.stance == Stance::Crawl;
    frame.crouched = s.stance == Stance::Crouch;
    frame.health = s.health;
    frame.hasted = s.hasteTime > 0.0f;
    frame.shrouded = s.shroudTime > 0.0f;
    frame.alignedChamber = gunState().aligned;
    frame.grinding = false;
    for (const ElementVolume& v : m_volumes.all()) {
        const ElementDef& def = m_ammo.elements[v.element];
        if (def.damage == DamageKind::Fire && def.driftSpeed <= 0.0f && def.volumeLifetime > 1.0f) {
            frame.fires.push_back({v.id, v.center, std::clamp(v.remaining01() * 3.0f, 0.0f, 1.0f), 1.0f});
        }
    }
    for (std::size_t i = 0; i < m_vortices.size(); ++i) {
        const WindVortex& w = m_vortices[i];
        frame.infernos.push_back({static_cast<std::uint32_t>(i + 1), w.base + glm::vec3(0.0f, 1.5f, 0.0f), w.strength,
                                  0.85f + 0.15f * w.strength});
    }
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (wispLike(ghost) && !m_ghosts.def(ghost).invisible) {
            const bool rushing = ghost.state == GhostState::Rush;
            frame.wisps.push_back({ghost.id, ghost.position, rushing ? 1.0f : 0.7f, rushing ? 1.5f : 1.0f});
        }

        auto heard = std::find_if(m_heardGhostStates.begin(), m_heardGhostStates.end(),
                                  [&](const auto& entry) { return entry.first == ghost.id; });
        if (heard == m_heardGhostStates.end()) {
            m_heardGhostStates.emplace_back(ghost.id, ghost.state);
            heard = m_heardGhostStates.end() - 1;
        }
        if (ghost.state == GhostState::Lift && heard->second != GhostState::Lift) {
            m_audio.play("poltergeist.lift", SoundGroup::Ghosts, 1.0f, ghost.position);
        }
        heard->second = ghost.state;
    }
    std::erase_if(m_heardGhostStates, [this](const auto& entry) { return m_ghosts.find(entry.first) == nullptr; });
    m_audio.update(frame);
}
void PlayView::updateGhostFx(float dt) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    updateMimicBodies(dt);
    updateFlocks(dt);

    for (ArcMark& mark : m_arcMarks) {
        mark.age += dt;
    }
    std::erase_if(m_arcMarks, [](const ArcMark& m) { return m.age > m.delay + 0.05f; });
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (m_ghosts.def(ghost).behavior != GhostBehavior::BallLightning || ghost.state == GhostState::Hop) {
            continue;
        }
        m_ballArcBudget += dt * 9.0f;
        while (m_ballArcBudget >= 1.0f) {
            m_ballArcBudget -= 1.0f;
            const float radius = m_ghosts.def(ghost).radius;
            const glm::vec3 out = glm::normalize(glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f) + glm::vec3(0.0f, 1e-3f, 0.0f));
            ActiveBolt bolt;
            bolt.from = ghostShownAt(ghost) + out * (radius * 0.9f);
            bolt.to = bolt.from + glm::normalize(out + glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f) * 0.8f) * (radius * (0.6f + 0.9f * unit(m_fxRng)));
            bolt.flashTimes = {0.0f};
            bolt.width = 0.008f;
            bolt.power = 0.7f;
            BoltParams params;
            params.levels = 3;
            params.jaggedness = 0.3f;
            params.branches = 1;
            params.branchLength = 0.3f;
            addBolt(std::move(bolt), params);
        }
    }

    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (ghost.hitstop > 0.0f || !wispLike(ghost)) {
            continue;
        }
        const float glow = wispGlow(ghost);
        const bool rushing = ghost.state == GhostState::Rush;
        m_ghostFxBudget += dt * (rushing ? 40.0f : 9.0f) * glow;
        while (m_ghostFxBudget >= 1.0f) {
            m_ghostFxBudget -= 1.0f;

            spawnSparks(ghost.position + glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f) * 0.4f, 1,
                        rushing ? 3.5f : 1.6f, 0.6f, wispColor(ghost));
            Spark& fleck = m_sparks.back();
            fleck.gravity = 0.15f;
            fleck.size *= 0.4f;
            fleck.life = 0.25f + unit(m_fxRng) * 0.45f;
        }
        if (unit(m_fxRng) < dt * (rushing ? 30.0f : 5.0f)) {
            spawnSparks(ghost.position, 1, 0.5f, 1.0f, wispColor(ghost));
            Spark& mote = m_sparks.back();
            mote.gravity = -0.05f;
            mote.size *= 0.5f;
            mote.life = 0.6f + unit(m_fxRng) * 0.8f;
        }
    }

    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (!wispLike(ghost)) {
            continue;
        }

        auto it = std::find_if(m_wispClouds.begin(), m_wispClouds.end(),
                               [&](const WispCloud& c) { return c.id == ghost.id && c.dispersing <= 0.0f; });
        if (it == m_wispClouds.end()) {
            m_wispClouds.push_back({ghost.id, ghost.position});
            it = m_wispClouds.end() - 1;
        }
        WispCloud& cloud = *it;
        cloud.alive = true;
        cloud.apparition = m_ghosts.def(ghost).invisible;
        cloud.radius = m_ghosts.def(ghost).radius * (cloud.apparition ? 1.8f : kWispMistScale);
        cloud.show = std::max(0.0f, cloud.show - dt / 1.6f);
        cloud.glow = cloud.apparition ? 1.0f : wispGlow(ghost);
        cloud.seed = ghost.seed;
        if (ghost.hitstop > 0.0f) {
            continue;
        }
        const glm::vec3 lag = ghost.position - cloud.center;

        cloud.velocity += (lag * 60.0f - cloud.velocity * 6.0f) * dt;
        cloud.center += cloud.velocity * dt;
        const float stir = glm::length(lag) * 3.0f + glm::length(cloud.velocity) * 0.3f;
        const float rush = ghost.state == GhostState::Rush ? 1.0f : 0.0f;
        cloud.phase += dt * (1.5f + 2.2f * rush + stir);
        cloud.flow += dt * (0.26f + 0.3f * rush + 0.12f * stir);
    }
    std::erase_if(m_wispClouds, [](const WispCloud& c) { return c.dispersing >= kWispDisperseTime; });
    for (WispCloud& cloud : m_wispClouds) {
        if (!cloud.alive) {
            cloud.dispersing += dt;
            cloud.velocity *= std::exp(-2.0f * dt);
            cloud.center += (cloud.velocity + m_play.upAt(cloud.center) * 0.35f) * dt;
            cloud.phase += dt * 2.0f;
            cloud.flow -= dt * 0.45f;
        }
        cloud.alive = false;
    }

    for (Absorb& a : m_absorbs) {
        a.age += dt;
        a.target = absorbTarget(a.player);
        const float t = std::min(a.age / a.life, 1.0f);
        const float ease = t * t * (3.0f - 2.0f * t);
        if (a.look == MaterialDef::Look::StormOrb) {
            a.position = glm::mix(a.from, a.target, ease);
            if (unit(m_fxRng) < dt * 30.0f) {
                spawnSparks(a.position, 1, 0.6f, 1.0f, glm::vec3(0.75f, 0.7f, 1.0f));
                m_sparks.back().gravity = 0.0f;
                m_sparks.back().size *= 0.4f;
                m_sparks.back().life = 0.2f;
            }
            continue;
        }
        if (a.look != MaterialDef::Look::Glow) {
            a.position = a.from;
            continue;
        }
        const float angle = a.seed + t * 9.0f;
        const float radius = 0.55f * std::sin(t * glm::pi<float>());
        a.position = glm::mix(a.from, a.target, ease) + glm::vec3(std::cos(angle) * radius, 0.25f * std::sin(t * glm::pi<float>()), std::sin(angle) * radius);
        spawnSparks(a.position, 1, 0.15f, 1.0f, a.color);
        Spark& mote = m_sparks.back();
        mote.gravity = 0.0f;
        mote.size *= 0.45f;
        mote.life = 0.35f;
    }
    std::erase_if(m_absorbs, [](const Absorb& a) { return a.age >= a.life; });

    const float lost = 1.0f - m_world.health;
    m_hurtShown += (lost - m_hurtShown) * (1.0f - std::exp(-6.0f * dt));
    m_hurtFlash *= std::exp(-4.0f * dt);
    m_hitMark.age01 = std::max(0.0f, m_hitMark.age01 - dt / (m_hitMark.kind == HitMark::Kind::Kill ? 0.45f : 0.25f));
    m_post->settings().hurt = glm::vec2(m_hurtShown, m_hurtFlash);
}
void PlayView::setGhostLook(bool apparition) {
    if (apparition) {
        m_wispFx.setLook(1, glm::vec3(0.01f, 0.16f, 0.05f), glm::vec3(0.3f, 1.0f, 0.4f));
    } else {
        m_wispFx.setLook(0, glm::vec3(0.01f, 0.06f, 0.34f), glm::vec3(0.05f, 0.8f, 1.0f));
    }
}
void PlayView::drawWisps(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward) {
    if (m_ghosts.ghosts().empty() && m_wispClouds.empty()) {
        return;
    }
    const SceneDepthInfo depth{m_sceneDepthCopy, m_post->zNear(), m_post->zFar(),
                               engine::PostProcess::depthSplit()};
    m_wispFx.begin(viewProj, cameraPos, cameraForward, depth, m_fxTime);
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (!wispLike(ghost) || !inWindow(ghostShownAt(ghost))) {
            continue;
        }
        const bool rushing = ghost.state == GhostState::Rush;
        const float heat = rushing ? std::min(1.0f, ghost.stateTime * 4.0f) : 0.0f;
        const auto it = std::find_if(m_wispClouds.begin(), m_wispClouds.end(),
                                     [&](const WispCloud& c) { return c.id == ghost.id && c.dispersing <= 0.0f; });
        const glm::vec3 heart = ghostShownAt(ghost);
        const glm::vec3 cloud = (it != m_wispClouds.end() ? it->center : ghost.position) + (heart - ghost.position);

        const glm::vec3 tail = ghost.up * 0.7f - (ghost.position - cloud) * 2.5f - ghost.velocity * 0.15f;
        if (m_ghosts.def(ghost).invisible) {
            continue;
        }
        setGhostLook(false);
        m_wispFx.draw(heart, cloud, tail, m_ghosts.def(ghost).radius * kWispMistScale, wispGlow(ghost), heat, ghost.seed,
                      it != m_wispClouds.end() ? it->phase : 0.0f, it != m_wispClouds.end() ? it->flow : 0.0f);
    }

    for (const WispCloud& cloud : m_wispClouds) {
        if (cloud.dispersing <= 0.0f || cloud.apparition || !inWindow(cloud.center)) {
            continue;
        }
        setGhostLook(false);
        const float t = cloud.dispersing / kWispDisperseTime;
        m_wispFx.draw(cloud.center, cloud.center, m_play.upAt(cloud.center), cloud.radius * (1.0f + 1.6f * t),
                      cloud.glow * (1.0f - t) * (1.0f - t), 0.0f, cloud.seed, cloud.phase, cloud.flow, 0.0f);
    }
    m_wispFx.end();
}
float PlayView::disguiseShown(const Ghost& ghost, glm::vec3& shake) const {
    shake = glm::vec3(0.0f);
    if (ghost.disguise < 0) {
        return 0.0f;
    }
    const MimicParams& p = m_ghosts.def(ghost).mimic;
    switch (ghost.state) {
    case GhostState::Disguised:
        return 1.0f;
    case GhostState::Reveal:
        shake = glm::vec3(std::sin(ghost.age * 53.0f), 0.5f * std::sin(ghost.age * 47.0f), std::cos(ghost.age * 59.0f)) * 0.04f;
        return 1.0f - glm::smoothstep(0.0f, 0.45f, ghost.stateTime / std::max(p.revealTime, 0.01f));
    case GhostState::Conceal:
        return glm::smoothstep(0.45f, 1.0f, ghost.stateTime / std::max(p.concealTime, 0.01f));
    default:
        return 0.0f;
    }
}
std::vector<PlayView::ShownDrop> PlayView::shownDrops() const {
    std::vector<ShownDrop> shown;
    shown.reserve(m_materialDrops.size());
    for (const MaterialDrop& drop : m_materialDrops) {
        shown.push_back({drop.material, drop.position, drop.age, 1.0f, drop.resting});
    }
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        const GhostDef& def = m_ghosts.def(ghost);
        glm::vec3 shake{0.0f};
        const float scale = def.behavior == GhostBehavior::Mimic ? disguiseShown(ghost, shake) : 0.0f;
        if (scale <= 0.01f || ghost.disguise >= kDisguiseBox) {
            continue;
        }
        shown.push_back({static_cast<MaterialId>(ghost.disguise), ghost.position + shake, ghost.age, scale, ghost.state == GhostState::Disguised});
    }
    return shown;
}
void PlayView::updateFlocks(float dt) {
    for (auto it = m_flocks.begin(); it != m_flocks.end();) {
        if (m_ghosts.find(it->first)) {
            ++it;
            continue;
        }
        it->second.shatter();
        m_fallenFlocks.push_back(std::move(it->second));
        it = m_flocks.erase(it);
    }
    for (ShardFlock& fallen : m_fallenFlocks) {
        fallen.fall(dt);
    }
    std::erase_if(m_fallenFlocks, [](const ShardFlock& f) { return f.empty(); });
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        const GhostDef& def = m_ghosts.def(ghost);
        if (def.behavior != GhostBehavior::Vasskraka) {
            continue;
        }
        ShardFlock::Input in;
        in.center = ghostShownAt(ghost);
        in.velocity = ghost.velocity;
        in.normal = ghost.surfaceNormal;
        in.up = ghost.up;
        in.size = vasskrakaSize(ghost, def);
        in.seed = ghost.seed;
        switch (ghost.state) {
        case GhostState::Roost: in.form = ghost.attached ? ShardFlock::Form::Cluster : ShardFlock::Form::Raven; break;
        case GhostState::Stoop:

            in.form = ghost.stateTime < def.vasskraka.diveWindup ? ShardFlock::Form::Raven : ShardFlock::Form::Stoop;
            in.flare = ghost.stateTime < def.vasskraka.diveWindup ? glm::smoothstep(0.0f, def.vasskraka.diveWindup * 0.4f, ghost.stateTime) : 0.0f;
            if (ghost.stateTime < def.vasskraka.diveWindup && !ghost.posed) {
                const glm::vec3 toward = ghost.goal - ghost.position;
                in.velocity = glm::length(toward) > 1e-3f ? glm::normalize(toward) * 2.0f : in.velocity;
            }
            break;
        case GhostState::Gather:
        case GhostState::Fall: in.form = ShardFlock::Form::Ball; break;
        case GhostState::Scatter:
        case GhostState::Merge: in.form = ShardFlock::Form::Cloud; break;
        default: in.form = ShardFlock::Form::Raven; break;
        }
        if (ghost.caught > 0.0f) {
            in.form = ShardFlock::Form::Cloud;
        }
        if (ghost.hitstop > 0.0f) {
            continue;
        }
        m_flocks[ghost.id].update(dt, in);
    }
}
void PlayView::drawFlocks(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    if (m_flocks.empty() && m_fallenFlocks.empty()) {
        return;
    }
    std::vector<Shards::Light> lights;
    for (const FogLight& light : m_frameLights) {
        lights.push_back({light.position, light.color});
    }
    m_shardsFx.begin(viewProj, cameraPos, glm::normalize(m_lightDir), glm::vec3(3.0f, 2.9f, 2.7f), lights);
    for (const auto& [id, flock] : m_flocks) {
        m_shardsFx.draw(flock.shards());
        m_shardsFx.draw(flock.debris());
    }
    for (const ShardFlock& fallen : m_fallenFlocks) {
        m_shardsFx.draw(fallen.debris());
    }
    m_shardsFx.end();
}
void PlayView::updateMimicBodies(float dt) {
    for (auto it = m_mimics.begin(); it != m_mimics.end();) {
        it = m_ghosts.find(it->first) ? std::next(it) : m_mimics.erase(it);
    }
    const MimicBody::Raycast surface = [this](const glm::vec3& from, const glm::vec3& to) -> std::optional<glm::vec3> {
        if (const auto hit = m_physics.raycast(from, to)) {
            return hit->point;
        }
        return std::nullopt;
    };
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        const GhostDef& def = m_ghosts.def(ghost);
        if (def.behavior != GhostBehavior::Mimic) {
            continue;
        }
        MimicShown& shown = m_mimics[ghost.id];
        shown.lashAge += dt;
        MimicBody::Input in;
        in.center = ghostShownAt(ghost);
        in.velocity = ghost.velocity;
        in.surfaceNormal = ghost.surfaceNormal;
        in.bodyRadius = def.mimic.bodyRadius;
        in.lift = def.mimic.standHeight;
        in.seed = ghost.seed;
        in.target = shown.lashAt;
        const float windup = std::max(def.mimic.thrashWindup, 0.01f);
        if (ghost.caught > 0.0f) {
            in.mode = MimicBody::Mode::Tumble;
            in.lift = 0.0f;
        } else if (ghost.state == GhostState::Disguised) {
            in.mode = MimicBody::Mode::Hidden;
        } else if (ghost.state == GhostState::Reveal) {
            in.mode = MimicBody::Mode::Reveal;
            in.progress = ghost.stateTime / std::max(def.mimic.revealTime, 0.01f);
        } else if (ghost.state == GhostState::Conceal) {
            in.mode = MimicBody::Mode::Reveal;
            in.violence = 0.15f;
            in.progress = 1.0f - ghost.stateTime / std::max(def.mimic.concealTime, 0.01f);
        } else if (shown.lashAge < windup + 0.25f) {
            in.mode = MimicBody::Mode::Lash;
            in.progress = shown.lashAge / windup;
        } else if (!ghost.attached || ghost.state == GhostState::Leap) {
            in.mode = MimicBody::Mode::Air;
        } else if (glm::length(ghost.velocity) > 0.3f) {
            in.mode = MimicBody::Mode::Walk;
        } else {
            in.mode = MimicBody::Mode::Idle;
        }
        shown.body.update(dt, in, surface);
        if (shown.body.steps() > shown.steps + 2) {
            shown.steps = shown.body.steps();
            m_audio.play("mimic.step", SoundGroup::Ghosts, 0.5f, in.center);
        }
    }
}
void PlayView::drawMimics(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    if (m_mimics.empty()) {
        return;
    }
    m_mimicFx.begin(viewProj, cameraPos, engine::PostProcess::depthSplit(), m_fxTime);
    for (const auto& [id, shown] : m_mimics) {
        if (const Ghost* ghost = m_ghosts.find(id)) {
            const bool churning = ghost->state == GhostState::Reveal || ghost->state == GhostState::Conceal || shown.lashAge < 0.6f;
            m_mimicFx.draw(shown.body, ghost->seed, churning ? 1.0f : 0.0f);
        }
    }
    m_mimicFx.end();
}
void PlayView::drawMaterialOrbs(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    using Look = MaterialDef::Look;
    bool any = false;
    auto orb = [&](const glm::vec3& at, float radius, MaterialOrb::Kind kind, float seed, const MaterialOrb::Suck* suck) {
        if (!any) {
            m_materialOrb.begin(viewProj, cameraPos, engine::PostProcess::depthSplit(), m_fxTime);
            any = true;
        }
        m_materialOrb.draw(at, radius, kind, seed, suck);
    };
    std::vector<std::vector<Strands::Point>> threads;
    auto ribbons = [&](const glm::vec3& at, float seed, const MaterialOrb::Suck* suck) {
        for (int i = 0; i < kWindRibbons; ++i) {
            const auto ribbon = windRibbon(i, at, m_fxTime, seed, suck ? suck->detach[static_cast<std::size_t>(i)] : 0.0f,
                                           suck ? suck->target : at);
            m_strands.add(ribbon);
        }
    };

    for (const ShownDrop& drop : shownDrops()) {
        if (!inWindow(drop.position)) {
            continue;
        }
        const Look look = m_ammo.materials[drop.material].look;
        const glm::vec3 at = drop.position + glm::vec3(0.0f, 0.16f + 0.03f * std::sin(drop.age * 1.7f), 0.0f);
        const float seed = static_cast<float>(drop.material) * 3.7f;
        switch (look) {
        case Look::StormOrb: orb(at, 0.12f * drop.scale, MaterialOrb::Kind::StormOrb, seed, nullptr); break;
        case Look::HazeBubble: orb(at, 0.1f * drop.scale, MaterialOrb::Kind::HazeBubble, seed, nullptr); break;
        case Look::WindCloud:
            orb(at, 0.1f * drop.scale, MaterialOrb::Kind::WindCloud, seed, nullptr);
            if (drop.scale > 0.6f) {
                ribbons(at, seed, nullptr);
            }
            break;
        case Look::ThreadKnot:
            if (drop.scale > 0.6f) {
                threadKnot(at, m_fxTime, seed, 0.0f, at, threads);
            }
            break;
        default: break;
        }
    }

    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (m_ghosts.def(ghost).behavior == GhostBehavior::BallLightning && inWindow(ghostShownAt(ghost))) {
            const bool hopping = ghost.state == GhostState::Hop;
            if (!any) {
                m_materialOrb.begin(viewProj, cameraPos, engine::PostProcess::depthSplit(), m_fxTime);
                any = true;
            }
            m_materialOrb.drawBall(ghostShownAt(ghost), m_ghosts.def(ghost).radius * (hopping ? 0.6f : 1.0f), ghost.seed, hopping ? 2.2f : 1.0f);
        }
    }

    for (const Absorb& a : m_absorbs) {
        if (!inWindow(a.position)) {
            continue;
        }
        const float seed = static_cast<float>(a.material) * 3.7f;
        const float t = std::min(a.age / a.life, 1.0f);
        if (a.look == Look::StormOrb) {
            const float size = 0.12f * (1.0f - glm::smoothstep(0.6f, 1.0f, t));
            if (size > 0.004f) {
                orb(a.position, size, MaterialOrb::Kind::StormOrb, seed, nullptr);
            }
        } else if (a.look == Look::HazeBubble || a.look == Look::WindCloud) {
            MaterialOrb::Suck suck;
            suck.target = a.target;
            for (std::size_t i = 0; i < suck.detach.size(); ++i) {
                suck.detach[i] = std::clamp((a.age - 0.07f * static_cast<float>(i)) / 0.28f, 0.0f, 1.0f);
            }
            suck.stream = std::clamp((a.age - 0.45f) / 0.75f, 0.0f, 1.0f);
            if (a.look == Look::HazeBubble) {
                orb(a.position, 0.1f, MaterialOrb::Kind::HazeBubble, seed, &suck);
            } else {
                orb(a.position, 0.1f, MaterialOrb::Kind::WindCloud, seed, &suck);
                ribbons(a.position, seed, &suck);
            }
        } else if (a.look == Look::ThreadKnot) {
            threadKnot(a.position, m_fxTime, seed, t, a.target, threads);
        }
    }
    if (any) {
        m_materialOrb.end();
    }
    for (const auto& thread : threads) {
        m_strands.add(thread);
    }

    for (const Ghost& ghost : m_ghosts.ghosts()) {
        const GhostDef& def = m_ghosts.def(ghost);
        if (def.behavior != GhostBehavior::Necromite || !inWindow(ghost.position)) {
            continue;
        }
        const glm::vec3 ground = ghost.position - ghost.up * def.radius;
        glm::vec3 heading = ghost.velocity;
        glm::vec3 head = ground;
        if (const PlayerBody* body = m_play.bodyOf(ghost.quarryId)) {
            head = body->eye - body->up * 0.1f;
            if (glm::length(heading - ghost.up * glm::dot(heading, ghost.up)) < 0.1f) {
                heading = head - ground;
            }
        }
        WormPose pose = WormPose::Crawl;
        float progress = 0.0f;
        if (ghost.state == GhostState::Emerge) {
            pose = WormPose::Emerge;
            progress = ghost.stateTime / std::max(def.necromite.emergeTime, 0.01f);
        } else if (ghost.state == GhostState::Bore) {
            pose = WormPose::Bore;
            progress = ghost.stateTime / std::max(def.necromite.enterTime, 0.01f);
        } else if (ghost.state == GhostState::Burrow) {
            pose = WormPose::Burrow;
            progress = ghost.stateTime / std::max(def.necromite.emergeTime, 0.01f);
        }
        m_strands.add(necromiteWorm(ground, heading, pose, progress, m_fxTime, ghost.seed, head, ghost.up));
    }

    for (const auto& [id, flock] : m_flocks) {
        for (const ShardFlock::Trail& trail : flock.trails()) {
            if (trail.points.size() < 3 || !inWindow(trail.points.front())) {
                continue;
            }
            std::vector<Strands::Point> streak;
            streak.reserve(trail.points.size());
            for (std::size_t k = 0; k < trail.points.size(); ++k) {
                const float along = static_cast<float>(k) / static_cast<float>(trail.points.size() - 1);
                Strands::Point p;
                p.position = trail.points[k];
                p.width = trail.width * (1.0f - 0.8f * along);
                p.color = glm::vec3(0.015f, 0.018f, 0.03f);
                p.alpha = 0.6f * std::pow(1.0f - along, 1.5f) * glm::smoothstep(0.0f, 0.12f, along + 0.04f);
                streak.push_back(p);
            }
            m_strands.add(streak);
        }
    }
    m_strands.draw(viewProj, cameraPos);
}
void PlayView::drawGhosts(const glm::mat4& viewProj, const glm::mat4& invView) {
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (m_ghosts.def(ghost).invisible || !wispLike(ghost)) {
            continue;
        }

        drawFlash(viewProj, invView, ghostShownAt(ghost), 0.12f, glm::mix(wispColor(ghost), glm::vec3(1.0f), 0.7f),
                  2.2f * wispGlow(ghost), ghost.seed, 0.35f);
    }
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (m_ghosts.def(ghost).invisible || !wispLike(ghost)) {
            continue;
        }

        const glm::vec3 color = wispColor(ghost);
        const float glow = wispGlow(ghost);
        for (int i = 0; i < 3; ++i) {
            const float k = static_cast<float>(i);

            const float a = ghost.age * (2.2f + 0.5f * k) + ghost.seed + k * 2.1f;
            const float tilt = 0.5f + 0.9f * k;
            const float yaw = ghost.seed + k * 2.4f;
            const glm::vec3 ring{std::cos(a), std::sin(a) * std::sin(tilt), std::sin(a) * std::cos(tilt)};
            const glm::vec3 at = ghostShownAt(ghost) + glm::vec3(ring.x * std::cos(yaw) + ring.z * std::sin(yaw), ring.y,
                                                            -ring.x * std::sin(yaw) + ring.z * std::cos(yaw)) * (0.3f + 0.04f * k);
            drawFlash(viewProj, invView, at, 0.14f - 0.02f * k, color, 0.35f * glow, ghost.seed + k, 0.0f);
            drawFlash(viewProj, invView, at, 0.05f - 0.008f * k, glm::mix(color, glm::vec3(1.0f), 0.6f), 1.6f * glow, ghost.seed + k, 0.0f);
        }
    }
    for (const ShownDrop& drop : shownDrops()) {
        const glm::vec3 color = m_ammo.materials[drop.material].color;
        if (m_ammo.materials[drop.material].look != MaterialDef::Look::Glow) {
            continue;
        }
        const float pulse = 0.75f + 0.25f * std::sin(drop.age * 3.0f);
        drawFlash(viewProj, invView, drop.position, 0.5f * drop.scale, color, 0.6f * pulse, drop.age, 0.0f);
        drawFlash(viewProj, invView, drop.position, 0.16f * drop.scale, glm::mix(color, glm::vec3(1.0f), 0.5f), 2.4f * pulse, drop.age * 2.0f, 1.0f);
    }
    for (const ArcMark& mark : m_arcMarks) {
        const float t = std::min(mark.age / std::max(mark.delay, 0.01f), 1.0f);
        const float flicker = 0.5f + 0.5f * std::sin(m_fxTime * 60.0f + mark.seed);
        drawFlash(viewProj, invView, mark.position + glm::vec3(0.0f, 0.04f, 0.0f), 0.12f + 0.2f * t, glm::vec3(0.7f, 0.55f, 1.0f),
                  (0.6f + 1.6f * t) * flicker, mark.seed + std::floor(m_fxTime * 16.0f), 1.0f);
    }
    for (const Absorb& a : m_absorbs) {
        if (a.look != MaterialDef::Look::Glow) {
            continue;
        }
        const float t = a.age / a.life;
        drawFlash(viewProj, invView, a.position, 0.2f * (1.0f - 0.6f * t), glm::mix(a.color, glm::vec3(1.0f), 0.5f), 2.6f, a.seed, 1.0f);
    }
}
void PlayView::updateFogFx(float dt) {
    for (SteamBurst& burst : m_steam) {
        burst.age += dt;
    }
    std::erase_if(m_steam, [](const SteamBurst& s) { return s.age >= s.life; });

    for (SmokePuff& puff : m_smoke) {
        FogCloud& c = puff.cloud;
        c.age += dt;
        const glm::vec3 air = airVelocity(m_vortices, c.center);
        puff.velocity = air + (puff.velocity - air) * std::exp(-3.0f * dt);
        puff.velocity += m_play.upAt(c.center) * (0.1f * dt);
        c.center += puff.velocity * dt;
        const float swell = 1.0f + 0.5f * dt / std::max(c.lifetime, 0.1f);
        c.radius *= swell;
        c.height *= swell;
    }
    std::erase_if(m_smoke, [](const SmokePuff& p) { return p.cloud.age >= p.cloud.lifetime; });

    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::vec3 cold{0.55f, 0.7f, 1.0f};
    for (const FogCloud& cloud : m_fog.clouds()) {
        if (cloud.electrified <= 0.0f) {
            continue;
        }
        auto inCloud = [&] {
            const float a = unit(m_fxRng) * glm::two_pi<float>();
            const float r = cloud.radius * 0.8f * std::sqrt(unit(m_fxRng));
            return cloud.center + glm::vec3(std::cos(a) * r, cloud.height * (0.1f + 0.6f * unit(m_fxRng)), std::sin(a) * r);
        };
        m_fogArcBudget += dt * 34.0f;
        while (m_fogArcBudget >= 1.0f) {
            m_fogArcBudget -= 1.0f;
            ActiveBolt arc;
            arc.from = inCloud();
            arc.to = unit(m_fxRng) < 0.3f ? glm::vec3(arc.from.x + unit(m_fxRng) - 0.5f, cloud.center.y, arc.from.z + unit(m_fxRng) - 0.5f)
                                          : glm::mix(arc.from, inCloud(), 0.6f);
            arc.flashTimes = {0.0f, 0.04f + 0.05f * unit(m_fxRng)};
            arc.width = 0.03f;
            arc.power = 0.9f;
            arc.light = 0.0f;
            BoltParams params;
            params.levels = 5;
            params.jaggedness = 0.26f;
            params.branches = 2;
            addBolt(std::move(arc), params);
            if (unit(m_fxRng) < 0.5f) {
                spawnSparks(inCloud(), 2, 2.0f, 0.4f, cold);
            }
        }
    }
}
void PlayView::drawFog(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward) {
    if (m_fog.clouds().empty() && m_smoke.empty() && m_clouds.empty() && m_steam.empty() && m_wispClouds.empty()) {
        return;
    }
    const SceneDepthInfo depth{m_sceneDepthCopy, m_post->zNear(), m_post->zFar(),
                               engine::PostProcess::depthSplit()};

    if (!m_fog.clouds().empty() && m_window.holds(fogDistance(cameraPos))) {
        m_fogVolume.draw(m_fog.clouds(), m_fog.holes(), m_fogStyle, viewProj, cameraPos, cameraForward, depth,
                         m_frameLights, glm::normalize(m_lightDir), m_fxTime);
    }

    std::vector<FogCloud> storms;
    for (const StormCloud& storm : m_clouds) {
        if (!inWindow(storm.center)) {
            continue;
        }
        const float build = std::clamp(storm.age / storm.delay, 0.0f, 1.0f);
        const float after = std::max(0.0f, storm.age - storm.delay);
        FogCloud c;
        c.id = storm.id + 1000;
        c.ball = true;
        c.center = storm.center;
        c.radius = m_lightningTuning.cloudRadius * (0.45f + 0.75f * build);
        c.height = 1.1f + 0.5f * build;
        c.age = 1.0f + storm.age;
        c.lifetime = c.age + FogField::kFadeTime * std::max(0.0f, std::min(build * 1.5f, 1.0f - after));
        storms.push_back(c);
    }
    if (!storms.empty()) {
        FogStyle storm;
        storm.density = 3.4f;
        storm.steps = 28;
        storm.noiseScale = 0.8f;
        storm.churn = 2.5f;
        storm.brightness = 0.2f;
        m_fogVolume.draw(storms, {}, storm, viewProj, cameraPos, cameraForward, depth, m_frameLights,
                         glm::normalize(m_lightDir), m_fxTime);
    }

    std::vector<FogCloud> steamClouds;
    for (const SteamBurst& burst : m_steam) {
        if (!inWindow(burst.center)) {
            continue;
        }
        const float t = burst.age / burst.life;
        const float swell = 1.0f + 1.1f * (1.0f - (1.0f - t) * (1.0f - t));
        FogCloud c;
        c.id = burst.id + 5000;
        c.center = burst.center + m_play.upAt(burst.center) * (1.2f * t);
        c.radius = burst.radius * swell;
        c.height = burst.height * (swell + 0.5f * t);
        c.age = 2.0f + burst.age;
        c.lifetime = c.age + FogField::kFadeTime * (1.0f - t) * (1.0f - t) * burst.strength;
        c.reach = burst.reach;

        c.reach[2] = std::max(c.reach[2] - 1.2f * t, 0.3f);
        c.reach[3] += 1.2f * t;
        steamClouds.push_back(c);
    }
    if (!steamClouds.empty()) {
        FogStyle steamStyle;
        steamStyle.density = 1.5f;
        steamStyle.steps = 36;
        steamStyle.noiseScale = 0.45f;
        steamStyle.churn = 5.0f;
        steamStyle.brightness = 1.5f;
        m_fogVolume.draw(steamClouds, {}, steamStyle, viewProj, cameraPos, cameraForward, depth, m_frameLights,
                         glm::normalize(m_lightDir), m_fxTime);
    }

    std::vector<FogCloud> apparitions;
    for (const WispCloud& cloud : m_wispClouds) {
        if (!cloud.apparition || !inWindow(cloud.center)) {
            continue;
        }
        const float t = cloud.dispersing / kWispDisperseTime;
        const float visible = cloud.dispersing > 0.0f ? (1.0f - t) * (1.0f - t) : cloud.show;
        if (visible <= 0.01f) {
            continue;
        }
        FogCloud c;
        c.id = cloud.id + 9000;
        c.ball = true;
        c.center = cloud.center;
        c.radius = cloud.radius * (1.0f + 1.4f * t);
        c.height = c.radius * 0.85f;
        c.age = 2.0f;
        c.lifetime = c.age + FogField::kFadeTime * visible;
        apparitions.push_back(c);
    }
    if (!apparitions.empty()) {
        FogStyle green;
        green.density = 6.5f;
        green.steps = 28;
        green.noiseScale = 2.4f;
        green.churn = 2.0f;
        green.brightness = 1.5f;
        green.tint = glm::vec3(0.3f, 1.0f, 0.42f);
        m_fogVolume.draw(apparitions, {}, green, viewProj, cameraPos, cameraForward, depth, m_frameLights,
                         glm::normalize(m_lightDir), m_fxTime);
    }

    std::vector<FogCloud> puffs;
    for (const SmokePuff& puff : m_smoke) {
        if (inWindow(puff.cloud.center)) {
            puffs.push_back(puff.cloud);
        }
    }
    FogStyle style;
    style.density = 9.0f * m_effects.smokeOpacity / 0.5f;
    style.steps = 20;
    style.noiseScale = 4.5f;
    style.churn = 1.6f;
    m_fogVolume.draw(puffs, {}, style, viewProj, cameraPos, cameraForward, depth, m_frameLights,
                     glm::normalize(m_lightDir), m_fxTime);
}
void PlayView::updateLightning(float dt) {
    const LightningTuning& t = m_lightningTuning;
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const glm::vec3 boltColor{0.6f, 0.72f, 1.0f};

    for (StormCloud& cloud : m_clouds) {
        cloud.age += dt;
        if (cloud.struck) {
            continue;
        }
        const float build = std::clamp(cloud.age / cloud.delay, 0.0f, 1.0f);
        const float radius = t.cloudRadius * (0.35f + 0.65f * build);

        if (unit(m_fxRng) < dt * 14.0f * build) {
            const float a = unit(m_fxRng) * glm::two_pi<float>();
            const glm::vec3 radial{std::cos(a), 0.0f, std::sin(a)};
            spawnPuff(cloud.target + radial * 1.6f + glm::vec3(0.0f, 0.1f, 0.0f),
                      glm::vec3(radial.z, 0.1f, -radial.x) * 3.0f - radial * 2.0f, glm::vec3(0.4f, 0.38f, 0.36f),
                      0.6f, 0.1f, 0.5f, 0.25f);
        }

        cloud.flickerTimer -= dt;
        if (cloud.flickerTimer <= 0.0f) {
            cloud.flickerTimer = glm::mix(0.34f, 0.07f, build) * (0.5f + unit(m_fxRng));
            auto inCloud = [&] {
                const float a = unit(m_fxRng) * glm::two_pi<float>();
                return cloud.center + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * (radius * unit(m_fxRng)) +
                       glm::vec3(0.0f, (unit(m_fxRng) - 0.5f) * 0.6f, 0.0f);
            };
            ActiveBolt flicker;
            flicker.from = inCloud();
            flicker.to = inCloud();
            flicker.flashTimes = {0.0f};
            flicker.width = 0.04f;
            flicker.power = 0.35f + 0.4f * build;
            BoltParams params;
            params.levels = 4;
            params.jaggedness = 0.3f;
            params.branches = 2;
            addBolt(std::move(flicker), params);

            m_lightSpikes.push_back({cloud.center - glm::vec3(0.0f, 1.0f, 0.0f), boltColor * (4.0f + 8.0f * build), 0.0f, 0.1f});
        }
    }
    std::erase_if(m_clouds, [](const StormCloud& c) { return c.age > c.delay + 1.0f; });

    for (ActiveBolt& bolt : m_bolts) {
        bolt.age += dt;
        if (!bolt.struck) {
            continue;
        }
        if (bolt.flashesDone > 0 && bolt.hold > 0.0f) {
            bolt.hold -= dt;
            continue;
        }
        bolt.strikeAge += dt;
        while (bolt.flashesDone < static_cast<int>(bolt.flashTimes.size()) &&
               bolt.strikeAge >= bolt.flashTimes[static_cast<std::size_t>(bolt.flashesDone)]) {
            const float amplitude = std::pow(0.62f, static_cast<float>(bolt.flashesDone));
            ++bolt.flashesDone;
            bolt.jitterSeed = unit(m_fxRng) * 50.0f;
            m_frameFlash = std::max(m_frameFlash, bolt.frameFlash * amplitude);
            if (bolt.light > 0.0f) {
                m_lightSpikes.push_back({bolt.to + glm::vec3(0.0f, 0.3f, 0.0f), boltColor * bolt.light * amplitude, 0.0f,
                                         0.09f});
            }
        }
    }
    std::erase_if(m_bolts, [&](const ActiveBolt& b) {
        if (!b.struck) {
            return b.age > 6.0f;
        }
        return b.flashTimes.empty() || b.strikeAge > b.flashTimes.back() + t.afterglow;
    });

    m_frameFlash *= std::exp(-22.0f * dt);
    m_hitFlash *= std::exp(-5.0f * dt);
    m_post->settings().flash = std::min(m_frameFlash, 0.85f);
}
void PlayView::drawRibbons(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    if (m_ribbons.empty()) {
        return;
    }
    m_lightning.begin(viewProj, cameraPos);
    for (const RibbonPiece& r : m_ribbons) {
        if (!inWindow((r.from + r.to) * 0.5f)) {
            continue;
        }
        const float nearest = std::min(glm::distance(cameraPos, r.from), glm::distance(cameraPos, r.to));
        const float fade = 1.0f - r.age / kRibbonLife;
        const float intensity = 0.22f * fade * fade * glm::smoothstep(1.5f, 4.0f, nearest);
        if (intensity <= 0.005f) {
            continue;
        }
        Bolt line;
        line.paths.push_back({{r.from, r.to}, {1.0f, 1.0f}, {0.0f, 1.0f}});
        BoltStyle style;
        style.width = 0.006f;
        style.haloScale = 3.0f;
        style.color = glm::vec3(0.75f, 0.7f, 0.6f);
        style.intensity = intensity;
        m_lightning.draw(line, style);
    }
    m_lightning.end();
}
void PlayView::drawLightning(const glm::mat4& viewProj, const glm::vec3& cameraPos) {
    if (m_bolts.empty()) {
        return;
    }
    const LightningTuning& t = m_lightningTuning;
    m_lightning.begin(viewProj, cameraPos);
    for (const ActiveBolt& bolt : m_bolts) {
        if (!inWindow((bolt.from + bolt.to) * 0.5f)) {
            continue;
        }
        BoltStyle style;
        style.width = bolt.width;
        style.jitterSeed = bolt.jitterSeed;
        if (bolt.fromMuzzle) {
            style.startShift = m_lastMuzzleWorld - bolt.from;
            style.startWidth = 0.3f;
        }
        if (!bolt.struck) {
            if (bolt.leaderStart < 0.0f || bolt.age < bolt.leaderStart) {
                continue;
            }
            style.reveal = std::clamp((bolt.age - bolt.leaderStart) / t.leaderTime, 0.0f, 0.999f);
            style.intensity = 0.2f * bolt.power;
            style.width = bolt.width * 0.5f;
        } else {
            float brightness = 0.0f;
            for (int k = 0; k < bolt.flashesDone; ++k) {
                const float since = bolt.strikeAge - bolt.flashTimes[static_cast<std::size_t>(k)];
                brightness = std::max(brightness, std::pow(0.62f, static_cast<float>(k)) * std::exp(-since / t.flashDecay));
            }
            const float last = bolt.flashTimes.empty() ? 0.0f : bolt.flashTimes.back();
            const float glow = 0.025f * (bolt.strikeAge < last ? 1.0f : std::max(0.0f, 1.0f - (bolt.strikeAge - last) / t.afterglow));
            if (bolt.flashesDone == 0) {
                continue;
            }
            style.intensity = (brightness + glow) * bolt.power;
            style.branchIntensity = 0.18f;
            style.jitter = bolt.width * 0.6f;
            style.width = bolt.width * (0.45f + 0.55f * std::min(1.0f, brightness * 1.5f + 0.2f));
        }
        m_lightning.draw(bolt.bolt, style);
    }
    m_lightning.end();
}
void PlayView::gatherDistortion(const glm::mat4& viewProj, const glm::vec3& eye) {
    std::vector<engine::PostProcess::DistortionSource> sources;
    const glm::vec2 size = glm::vec2(m_viewport);

    float front = 0.0f;
    auto toScreen = [&](const glm::vec3& p, float worldRadius, glm::vec2& center, float& radiusPx) {
        const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
        if (clip.w < 0.2f) {
            return false;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        center = (ndc * 0.5f + 0.5f) * size;
        radiusPx = worldRadius * viewProj[1][1] / clip.w * size.y * 0.5f;
        front = std::max(clip.w - worldRadius, 0.1f);
        return radiusPx > 2.0f;
    };

    {
        std::vector<const Ghost*> mimics;
        for (const Ghost& ghost : m_ghosts.ghosts()) {
            if (m_ghosts.def(ghost).behavior == GhostBehavior::Mimic && glm::distance(ghost.position, eye) < 30.0f) {
                mimics.push_back(&ghost);
            }
        }
        std::sort(mimics.begin(), mimics.end(), [&](const Ghost* a, const Ghost* b) {
            return glm::distance(a->position, eye) < glm::distance(b->position, eye);
        });
        if (mimics.size() > 3) {
            mimics.resize(3);
        }
        for (const Ghost* ghost : mimics) {
            glm::vec3 shake{0.0f};
            const float disguised = disguiseShown(*ghost, shake);
            const auto shownIt = m_mimics.find(ghost->id);
            const float striking = shownIt != m_mimics.end() && shownIt->second.lashAge < 0.7f ? 1.0f - shownIt->second.lashAge / 0.7f : 0.0f;
            const bool flying = ghost->state == GhostState::Leap;
            const float out = 1.0f - disguised;
            const float worldRadius = glm::mix(0.7f, 1.5f, out);
            glm::vec2 c;
            float radiusPx = 0.0f;
            if (toScreen(ghost->position, worldRadius, c, radiusPx)) {
                const float strength = glm::mix(std::clamp(0.012f * radiusPx, 1.5f, 5.0f), std::clamp(0.03f * radiusPx, 3.0f, 10.0f), out) *
                                       (1.0f + 1.2f * striking + (flying ? 0.6f : 0.0f));
                sources.push_back({c, radiusPx, strength, engine::PostProcess::DistortionKind::Chroma, 0.0f, ghost->seed});
                sources.back().depth = std::max(front - 0.5f, 0.1f);
            }
        }
    }

    for (const InfernoBlast& b : m_blasts) {
        if (b.age > 0.45f) {
            continue;
        }
        const float t = b.age / 0.45f;
        glm::vec2 c;
        float reachPx = 0.0f;
        if (toScreen(b.center, m_inferno.shockRadius * b.scale * (1.0f - (1.0f - t) * (1.0f - t)), c, reachPx)) {
            sources.push_back({c, std::max(12.0f, reachPx * 0.25f), 14.0f * (1.0f - t), engine::PostProcess::DistortionKind::Ring,
                               reachPx, b.seed});
            sources.back().depth = front;
        }
    }

    for (const ShownDrop& drop : shownDrops()) {
        if (m_ammo.materials[drop.material].look != MaterialDef::Look::HazeBubble) {
            continue;
        }
        glm::vec2 c;
        float radiusPx = 0.0f;
        if (toScreen(drop.position + glm::vec3(0.0f, 0.16f, 0.0f), 0.24f * drop.scale, c, radiusPx)) {
            sources.push_back({c, radiusPx, std::min(4.0f, radiusPx * 0.3f), engine::PostProcess::DistortionKind::Lens, 0.0f, drop.age});
            sources.back().depth = front;
        }
    }
    for (const Absorb& a : m_absorbs) {
        const float left = 1.0f - std::clamp((a.age - 0.45f) / 0.75f, 0.0f, 1.0f);
        glm::vec2 c;
        float radiusPx = 0.0f;
        if (a.look == MaterialDef::Look::HazeBubble && left > 0.05f && toScreen(a.position, 0.24f * left, c, radiusPx)) {
            sources.push_back({c, radiusPx, std::min(4.0f, radiusPx * 0.3f), engine::PostProcess::DistortionKind::Lens, 0.0f, a.age});
            sources.back().depth = front;
        }
    }

    using Kind = engine::PostProcess::DistortionKind;

    {
        const SelfFx& fx = m_selfFx;
        if (fx.shroudCast < 0.5f) {
            const float t = fx.shroudCast / 0.5f;
            sources.push_back({size * 0.5f, size.y * (0.75f - 0.45f * t), 22.0f * (1.0f - t) * (1.0f - t), Kind::Lens, 0.0f, 3.0f});
        }
        if (fx.shroud > 0.02f) {
            for (int i = 0; i < 3; ++i) {
                const float a = m_fxTime * 0.35f + static_cast<float>(i) * 2.094f;
                const glm::vec2 c = size * 0.5f + glm::vec2(std::cos(a) * size.x * 0.36f, std::sin(a) * size.y * 0.36f);
                sources.push_back({c, size.y * 0.3f, 3.5f * fx.shroud, Kind::Haze, 0.0f, 11.0f + static_cast<float>(i) * 5.0f});
            }
        }
    }

    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (!m_ghosts.def(ghost).invisible) {
            continue;
        }
        const bool lifting = ghost.state == GhostState::Lift;
        glm::vec2 c;
        float radiusPx = 0.0f;
        if (toScreen(ghost.position, m_ghosts.def(ghost).radius * 1.6f, c, radiusPx)) {
            sources.push_back({c, radiusPx, std::min(lifting ? 7.0f : 3.5f, radiusPx * 0.4f), Kind::Lens, 0.0f, ghost.seed});
            sources.back().depth = front;
        }
        if (lifting && ghost.heldProp != kNoProp) {
            glm::vec3 position;
            glm::quat rotation;
            m_physics.pose(ghost.heldProp, position, rotation);
            if (toScreen(position, 0.7f, c, radiusPx)) {
                sources.push_back({c, radiusPx, std::min(8.0f, radiusPx * 0.4f), Kind::Haze, 0.0f, ghost.seed + 3.0f});
                sources.back().depth = front;
            }
        }
    }
    for (const HazeComet& comet : m_comets) {
        const float lens = m_hazeTuning.headRadius * 4.5f;
        const float tailAt[3] = {0.0f, 0.4f, 0.8f};
        const float scale[3] = {1.0f, 0.7f, 0.45f};
        for (int i = 0; i < 3; ++i) {
            glm::vec2 c;
            float radiusPx = 0.0f;
            if (toScreen(comet.head - comet.direction * (comet.tail * tailAt[i]), lens * scale[i], c, radiusPx)) {
                const float strength = std::min(m_hazeTuning.refraction, radiusPx * 0.45f) * scale[i] * comet.fade;
                sources.push_back({c, radiusPx, strength, Kind::Lens, 0.0f, comet.seed});
                sources.back().depth = front;
            }
        }
    }

    for (const BulletWake& wake : m_wakes) {
        if (sources.size() >= engine::PostProcess::kMaxDistortions - 2) {
            break;
        }

        glm::vec3 tail = wake.from;
        const float length = glm::distance(wake.from, wake.to);
        if (length < 2.5f) {
            continue;
        }
        const glm::vec3 along = (wake.to - wake.from) / length;
        tail += along * std::max(2.0f, length - 14.0f);
        const glm::vec4 a = viewProj * glm::vec4(tail, 1.0f);
        const glm::vec4 b = viewProj * glm::vec4(wake.to, 1.0f);
        if (a.w < 0.2f || b.w < 0.2f) {
            continue;
        }
        const float pixelsPerMeter = viewProj[1][1] * size.y * 0.5f;
        engine::PostProcess::DistortionSource streak;
        streak.kind = Kind::Streak;
        streak.center = (glm::vec2(a) / a.w * 0.5f + 0.5f) * size;
        streak.end = (glm::vec2(b) / b.w * 0.5f + 0.5f) * size;
        streak.radius = std::clamp(0.03f * pixelsPerMeter / a.w, 2.0f, 7.0f);
        streak.endRadius = std::clamp(0.03f * pixelsPerMeter / b.w, 2.0f, 7.0f);
        streak.strength = 2.5f * (1.0f - wake.fade / 0.12f);
        streak.depth = std::max(a.w - 0.3f, 0.1f);
        streak.endDepth = std::max(b.w - 0.3f, 0.1f);
        sources.push_back(streak);
    }
    for (const Ripple& ripple : m_ripples) {
        const float t = ripple.age / ripple.duration;
        glm::vec2 c;
        float reachPx = 0.0f;
        if (toScreen(ripple.point, ripple.radius * (1.0f - (1.0f - t) * (1.0f - t)), c, reachPx)) {
            sources.push_back({c, std::max(8.0f, reachPx * 0.4f), 7.0f * (1.0f - t), Kind::Ring, reachPx, ripple.seed});
            sources.back().depth = front;
        }
    }

    struct Candidate {
        float distance;
        engine::PostProcess::DistortionSource source;
    };
    std::vector<Candidate> haze;
    const ElementId fire = m_ammo.element("fire");
    const ElementId inferno = m_ammo.element("inferno");
    for (const InfernoBlast& b : m_blasts) {
        const TornadoInstance t = tornadoOf(b);
        const float height = t.height * std::min(t.grow, 1.0f);
        const glm::vec3 mid = b.center + glm::vec3(0.0f, height * 0.6f, 0.0f);
        glm::vec2 c;
        float r = 0.0f;
        if (toScreen(mid, std::max(t.topRadius * t.thick * 1.6f, height * 0.65f), c, r)) {
            haze.push_back({glm::distance(eye, mid),
                            {c, r, m_inferno.hazeStrength * t.intensity * (1.0f - 0.5f * t.rope),
                             engine::PostProcess::DistortionKind::Haze, 0.0f, b.seed}});
            haze.back().source.depth = front;
        }
    }
    for (const ElementVolume& v : m_volumes.all()) {
        if (v.element != fire && (v.element != inferno || !m_blasts.empty())) {
            continue;
        }
        const glm::vec3 above = v.center + glm::vec3(0.0f, v.radius * 1.4f + 0.3f, 0.0f);
        glm::vec2 c;
        float r = 0.0f;
        if (toScreen(above, v.radius * 1.4f + 0.3f, c, r)) {
            haze.push_back({glm::distance(eye, above),
                            {c, r, 2.5f * std::min(1.0f, v.remaining01() * 3.0f), engine::PostProcess::DistortionKind::Haze, 0.0f,
                             static_cast<float>(v.id)}});
            haze.back().source.depth = front;
        }
    }
    std::sort(haze.begin(), haze.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    for (const Candidate& h : haze) {
        if (sources.size() >= engine::PostProcess::kMaxDistortions) {
            break;
        }
        sources.push_back(h.source);
    }
    m_post->setDistortion(sources, m_fxTime);
}
void PlayView::toBodySpace(std::uint32_t body, glm::vec3& point, glm::vec3& normal) const {
    glm::vec3 position;
    glm::quat rotation;
    m_physics.pose(body, position, rotation);
    const glm::quat inverse = glm::inverse(rotation);
    point = inverse * (point - position);
    normal = inverse * normal;
}
void PlayView::toWorldSpace(std::uint32_t body, bool attached, glm::vec3& point, glm::vec3& normal) const {
    if (!attached) {
        return;
    }
    glm::vec3 position;
    glm::quat rotation;
    m_physics.pose(body, position, rotation);
    point = position + rotation * point;
    normal = rotation * normal;
}
void PlayView::spawnPuff(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& color, float life,
                     float startSize, float endSize, float opacity) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    m_puffs.push_back({position, velocity, color, 0.0f, life, startSize, endSize, opacity, unit(m_fxRng) * 6.28f});
    if (m_puffs.size() > kMaxPuffs) {
        m_puffs.erase(m_puffs.begin());
    }
}
void PlayView::updatePuffs(float dt) {
    for (Puff& p : m_puffs) {
        p.age += dt;
        p.position += p.velocity * dt;

        const glm::vec3 air = airVelocity(m_vortices, p.position);
        p.velocity = air + (p.velocity - air) * std::exp(-3.5f * dt);
        p.velocity += m_play.upAt(p.position) * (0.12f * dt);
    }
    std::erase_if(m_puffs, [](const Puff& p) { return p.age >= p.life; });
}
void PlayView::drawPuffs(const glm::mat4& viewProj, const glm::mat4& invView) {
    m_flashShader.set("uViewProj", viewProj);
    m_flashShader.set("uRight", glm::vec3(invView[0]));
    m_flashShader.set("uUp", glm::vec3(invView[1]));
    m_flashShader.set("uSpikes", 0.0f);
    for (const Puff& p : m_puffs) {
        if (!inWindow(p.position)) {
            continue;
        }
        const float t = p.age / p.life;
        const float grow = 1.0f - (1.0f - t) * (1.0f - t);
        m_flashShader.set("uCenter", p.position);
        m_flashShader.set("uSize", glm::mix(p.startSize, p.endSize, grow));
        m_flashShader.set("uColor", p.color);
        m_flashShader.set("uSeed", p.seed);
        m_flashShader.set("uOpacity", p.opacity * (1.0f - t) * std::min(1.0f, p.age * 30.0f));
        m_quad.draw();
    }
}
void PlayView::drawDecals(const glm::mat4& viewProj) {
    if (m_sceneDepthCopy == 0 || (m_scorches.empty() && m_marks.empty())) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    m_decalShader.use();
    glBindTextureUnit(0, m_sceneDepthCopy);
    m_decalShader.set("uSceneDepth", 0);
    m_decalShader.set("uSplit", engine::PostProcess::depthSplit());
    m_decalShader.set("uViewProj", viewProj);
    m_decalShader.set("uInvViewProj", glm::inverse(viewProj));
    for (const Scorch& sc : m_scorches) {
        glm::vec3 point = sc.point;
        glm::vec3 normal = sc.normal;
        toWorldSpace(sc.body, sc.attached, point, normal);
        const float fade = 1.0f - glm::smoothstep(60.0f, 90.0f, sc.age);
        drawProjected(point, normal, sc.radius * 2.0f, std::max(kScorchDepth, sc.radius * 0.6f), glm::vec3(0.015f, 0.012f, 0.01f),
                      0.8f * fade, sc.seed, 0);
    }
    for (const ImpactMark& mark : m_marks) {
        if (mark.surface == Surface::Steel) {
            continue;
        }
        glm::vec3 point = mark.point;
        glm::vec3 normal = mark.normal;
        toWorldSpace(mark.body, mark.attached, point, normal);
        drawProjected(point, normal, kHoleSize, kHoleDepth, glm::vec3(0.0f), 0.92f, mark.spin * 3.0f, 1);
    }
    glCullFace(GL_BACK);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
void PlayView::drawProjected(const glm::vec3& center, const glm::vec3& normal, float size, float depth, const glm::vec3& color,
                             float opacity, float seed, int kind) {
    const glm::mat4 basis = basisFacing(normal);
    m_decalShader.set("uCenter", center);
    m_decalShader.set("uRight", glm::vec3(basis[0]));
    m_decalShader.set("uUp", glm::vec3(basis[1]));
    m_decalShader.set("uNormal", glm::vec3(basis[2]));
    m_decalShader.set("uSize", size);
    m_decalShader.set("uDepth", depth);
    m_decalShader.set("uColor", color);
    m_decalShader.set("uOpacity", opacity);
    m_decalShader.set("uSeed", seed);
    m_decalShader.set("uKind", kind);
    m_unitBox.draw();
}
bool PlayView::inWindow(const glm::vec3& point) const { return m_window.holds(glm::distance(point, m_windowEye)); }
float PlayView::fogDistance(const glm::vec3& eye) const {
    float nearest = 1e9f;
    for (const FogCloud& cloud : m_fog.clouds()) {
        nearest = std::min(nearest, glm::distance(cloud.center, eye));
    }
    return nearest;
}
void PlayView::drawSeeThrough(const glm::mat4& viewProj, const glm::mat4& invView, const glm::vec3& cameraPos, const glm::vec3& cameraForward) {
    m_windowEye = cameraPos;
    std::vector<float> big;
    auto at = [&](const glm::vec3& point) { big.push_back(glm::distance(point, cameraPos)); };
    for (const InfernoBlast& b : m_blasts) {
        at(b.center);
    }
    if (!m_fog.clouds().empty()) {
        big.push_back(fogDistance(cameraPos));
    }
    for (const StormCloud& storm : m_clouds) {
        at(storm.center);
    }
    for (const SteamBurst& burst : m_steam) {
        at(burst.center);
    }
    for (const WispCloud& cloud : m_wispClouds) {
        if (cloud.apparition || cloud.dispersing > 0.0f) {
            at(cloud.center);
        }
    }
    for (const SmokePuff& puff : m_smoke) {
        at(puff.cloud.center);
    }
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        const GhostBehavior behavior = m_ghosts.def(ghost).behavior;
        if (wispLike(ghost) || behavior == GhostBehavior::BallLightning) {
            at(ghostShownAt(ghost));
        } else if (behavior == GhostBehavior::Necromite) {
            at(ghost.position);
        }
    }
    for (const ShownDrop& drop : shownDrops()) {
        at(drop.position);
    }
    for (const Absorb& a : m_absorbs) {
        at(a.position);
    }
    for (const SeeThrough& extra : m_extras) {
        if (extra.draw) {
            big.push_back(extra.distance);
        }
    }
    for (const DrawWindow& window : drawSlices(std::move(big))) {
        m_window = window;
        drawTornadoes(viewProj, cameraPos, cameraForward);
        drawFog(viewProj, cameraPos, cameraForward);
        drawWisps(viewProj, cameraPos, cameraForward);
        drawMaterialOrbs(viewProj, cameraPos);
        drawEffects(viewProj, invView);
        drawElementFx(viewProj, invView);
        for (const SeeThrough& extra : m_extras) {
            if (m_window.holds(extra.distance) && extra.draw) {
                extra.draw();
                engine::PostProcess::useWorldDepthRange();
            }
        }
        for (const SeeThrough& extra : m_extras) {
            if (extra.span) {
                extra.span(m_window);
                engine::PostProcess::useWorldDepthRange();
            }
        }
    }
    m_window = DrawWindow{};
}
void PlayView::drawTracers(const glm::mat4& viewProj, float alpha) {
    (void)viewProj;
    if (m_showTracers) {
        m_litShader.use();
        setSurface(m_litShader, glm::vec3(0.0f), 0.0f, 1.0f, glm::vec3(4.0f, 2.6f, 1.2f));
        for (const Projectile& p : m_ballistics.projectiles()) {
            const glm::vec3 tail = p.previousPosition;
            const glm::vec3 head = glm::mix(p.previousPosition, p.position, alpha);
            const glm::vec3 d = head - tail;
            const float length = glm::length(d);
            if (length < 1e-3f) {
                continue;
            }
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), (head + tail) * 0.5f) * basisFacing(d / length) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(0.004f, 0.004f, length * 0.5f));
            drawWithModel(m_litShader, m_unitBox, model);
        }
        m_litShader.set("uEmissive", glm::vec3(0.0f));
    }
}
void PlayView::drawEffects(const glm::mat4& viewProj, const glm::mat4& invView) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    m_flashShader.use();
    drawPuffs(viewProj, invView);
    const float s = m_effects.impactScale;
    for (const ImpactFlash& f : m_impactFlashes) {
        const float t = f.age / f.duration;
        const bool steel = f.surface == Surface::Steel;
        const glm::vec3 color = f.color.x >= 0.0f ? f.color : impactColor(f.surface);
        const float spikes = f.spikes >= 0.0f ? f.spikes : (steel ? 1.0f : 0.25f);
        drawFlash(viewProj, invView, f.point, glm::mix(0.15f, 0.45f, t) * s * f.sizeScale, color,
                  (1.0f - t) * (steel ? 2.6f : 1.3f), f.seed, spikes);
    }
    drawGhosts(viewProj, invView);

    for (const Spark& sp : m_sparks) {
        const float t = sp.age / sp.life;
        const glm::vec3 color = glm::mix(sp.color, sp.coolTo, t);
        drawFlash(viewProj, invView, sp.position, sp.size * (1.0f - 0.4f * t), color, 2.6f * (1.0f - t * t), 0.0f,
                  0.0f);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
TornadoInstance PlayView::tornadoOf(const InfernoBlast& b) const {
    const InfernoTuning& t = m_inferno;
    TornadoInstance i;
    i.base = b.center;
    i.axis = b.normal;
    i.groundCenter = b.groundAt;
    i.groundSpan = kInfernoGroundSpan * b.scale;
    for (std::size_t k = 0; k < kTornadoGroundCells; ++k) {
        i.ground[k] = b.ground[k] > -100.0f ? b.ground[k] + (b.groundBase - glm::dot(b.center, tornadoBasis(b.normal)[1])) : b.ground[k];
    }
    i.height = t.height * b.scale;
    i.baseRadius = t.baseRadius * b.scale;
    i.topRadius = t.topRadius * b.scale;
    i.seed = b.seed;

    const float touchdown = b.age / std::max(t.touchdownTime, 0.05f);
    const float rise = std::clamp(touchdown / 0.6f, 0.0f, 1.0f);
    i.grow = 1.3f * (1.0f - (1.0f - rise) * (1.0f - rise));
    const float thicken = glm::smoothstep(0.35f, 1.0f, touchdown);
    const float burn = glm::smoothstep(t.rageTime, t.rageTime + 1.0f, b.age);
    const float left = b.duration - b.age;
    i.rope = 1.0f - std::clamp(left / std::max(t.ropeOutTime, 0.05f), 0.0f, 1.0f);
    i.thick = glm::mix(0.18f, glm::mix(1.0f, 0.8f, burn), thicken) * glm::mix(1.0f, 0.25f, i.rope * i.rope);
    i.intensity = std::clamp(left / 0.4f, 0.0f, 1.0f);
    return i;
}
void PlayView::drawTornadoes(const glm::mat4& viewProj, const glm::vec3& cameraPos, const glm::vec3& cameraForward) {
    if (m_blasts.empty()) {
        return;
    }
    std::vector<TornadoInstance> tornadoes;
    for (const InfernoBlast& b : m_blasts) {
        if (inWindow(b.center)) {
            tornadoes.push_back(tornadoOf(b));
        }
    }
    const SceneDepthInfo depth{m_sceneDepthCopy, m_post->zNear(), m_post->zFar(),
                               engine::PostProcess::depthSplit()};
    m_tornado.draw(tornadoes, m_inferno.style, viewProj, cameraPos, cameraForward, depth, m_fxTime);
}
void PlayView::drawFlashAxes(const glm::mat4& viewProj, const glm::vec3& center, const glm::vec3& right,
                         const glm::vec3& up, float size, const glm::vec3& color, float intensity, float seed,
                         float spikes, float opacity) {
    m_flashShader.set("uViewProj", viewProj);
    m_flashShader.set("uCenter", center);
    m_flashShader.set("uRight", right);
    m_flashShader.set("uUp", up);
    m_flashShader.set("uSize", size);
    m_flashShader.set("uColor", color);
    m_flashShader.set("uIntensity", intensity);
    m_flashShader.set("uSeed", seed);
    m_flashShader.set("uSpikes", spikes);
    m_flashShader.set("uOpacity", opacity);
    m_quad.draw();
}
void PlayView::setFrameLights(const glm::vec3& muzzleWorld) {
    m_litShader.use();

    struct Light {
        glm::vec3 position;
        glm::vec3 color;
    };
    std::vector<Light> lights;

    const float flash = std::max(m_flashTime, 0.0f) / m_effects.flashTime;
    lights.push_back({muzzleWorld, glm::vec3(1.0f, 0.62f, 0.3f) * m_effects.flashLight * flash * flash});

    for (const LightSpike& spike : m_lightSpikes) {
        const float t = spike.age / spike.duration;
        lights.push_back({spike.position, spike.color * (1.0f - t) * (1.0f - t)});
    }

    const ElementId fire = m_ammo.element("fire");
    const ElementId inferno = m_ammo.element("inferno");
    for (const ElementVolume& v : m_volumes.all()) {
        if (v.element != fire && v.element != inferno) {
            continue;
        }
        const float id = static_cast<float>(v.id);
        const float flick = 1.0f + 0.22f * std::sin(m_fxTime * 7.3f + id) + 0.14f * std::sin(m_fxTime * 13.7f + 1.3f + id);
        const float fade = std::min(1.0f, v.remaining01() * 4.0f) * std::min(1.0f, v.age * 6.0f);
        const glm::vec3 color = v.element == inferno ? glm::vec3(1.0f, 0.42f, 0.16f) : glm::vec3(1.0f, 0.55f, 0.22f);

        const float lift = v.element == inferno ? 1.6f : 0.25f;
        lights.push_back({v.center + v.normal * lift, color * 1.6f * (v.radius * 2.0f + 0.5f) * flick * fade});
    }
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        if (wispLike(ghost)) {
            lights.push_back({ghost.position, wispColor(ghost) * (1.4f * wispGlow(ghost))});
        } else if (m_ghosts.def(ghost).behavior == GhostBehavior::Mimic && ghost.state != GhostState::Disguised) {
            lights.push_back({ghost.position, glm::vec3(0.6f, 0.02f, 0.02f) * (0.5f + 0.2f * std::sin(m_fxTime * 2.6f + ghost.seed))});
        } else if (m_ghosts.def(ghost).behavior == GhostBehavior::BallLightning) {
            const float flicker = 0.7f + 0.6f * std::abs(std::sin(m_fxTime * 41.0f + ghost.seed) * std::sin(m_fxTime * 17.0f));
            lights.push_back({ghost.position, glm::vec3(0.7f, 0.55f, 1.0f) * (3.0f * flicker)});
        }
    }
    for (const ShownDrop& drop : shownDrops()) {
        float strength = 0.8f;
        if (m_ammo.materials[drop.material].look == MaterialDef::Look::StormOrb) {
            const float step = std::floor(m_fxTime * 16.0f);
            const float h = std::abs(std::fmod(std::sin(step * 12.9898f + static_cast<float>(drop.material)) * 43758.5453f, 1.0f));
            strength *= 0.4f + 1.1f * h;
        } else if (m_ammo.materials[drop.material].look == MaterialDef::Look::SmallFire) {
            strength *= 0.8f + 0.25f * std::sin(m_fxTime * 17.0f) * std::sin(m_fxTime * 7.3f + static_cast<float>(drop.material));
        }
        lights.push_back({drop.position + glm::vec3(0.0f, 0.16f, 0.0f), m_ammo.materials[drop.material].color * (strength * drop.scale)});
    }
    for (const Absorb& a : m_absorbs) {
        if (a.look == MaterialDef::Look::SmallFire) {
            const float burn = 1.0f - glm::smoothstep(0.45f, 1.0f, a.age / a.life);
            lights.push_back({a.from + glm::vec3(0.0f, 0.15f, 0.0f), a.color * ((0.8f + 3.0f * absorbFlare(a)) * burn)});
        }
    }

    if (lights.size() > 8) {
        const glm::vec3 eye = m_world.position;
        std::sort(lights.begin() + 1, lights.end(), [&](const Light& a, const Light& b) {
            return glm::distance(a.position, eye) < glm::distance(b.position, eye);
        });
        lights.resize(8);
    }
    m_frameLights.clear();
    for (const Light& light : lights) {
        m_frameLights.push_back({light.position, light.color});
    }
    m_litShader.set("uPointCount", static_cast<int>(lights.size()));
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const std::string index = std::to_string(i);
        m_litShader.set(("uPointPos[" + index + "]").c_str(), lights[i].position);
        m_litShader.set(("uPointColor[" + index + "]").c_str(), lights[i].color);
    }
}
void PlayView::consumeEvents() {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (const GameEvent& event : *m_eventsIn) {
        if (const auto* theirs = std::get_if<ShotFired>(&event); theirs && theirs->player != m_local) {
            if (const PlayerBody* other = m_play.bodyOf(theirs->player)) {
                const glm::vec3 muzzle = other->eye + other->viewDirection * 0.6f;
                if (m_others.recoil) {
                    m_others.recoil(theirs->player);
                }
                ImpactFlash flash{muzzle + other->viewDirection * 0.05f, Surface::Steel, 0.0f, unit(m_fxRng) * 6.28f};
                flash.color = glm::vec3(1.0f, 0.75f, 0.4f);
                flash.sizeScale = 2.0f;
                flash.duration = 0.07f;
                flash.spikes = 1.0f;
                m_impactFlashes.push_back(flash);
                m_lightSpikes.push_back({muzzle, glm::vec3(1.0f, 0.7f, 0.4f) * m_effects.flashLight, 0.0f, 0.08f});
                const ElementDef& theirRound = m_ammo.elements[theirs->round.element];
                const bool cold = theirRound.hitscanRange > 0.0f || theirRound.self != SelfEffect::None || theirRound.breathRange > 0.0f;
                m_audio.play(cold ? "revolver.discharge" : "revolver.shot", SoundGroup::Weapon, 1.0f, muzzle);
            }
            continue;
        }
        if (const auto* hurt = std::get_if<PlayerDamaged>(&event); hurt && hurt->player != static_cast<int>(m_local)) {
            if (const PlayerBody* body = m_play.bodyOf(static_cast<PlayerId>(hurt->player))) {
                const glm::vec3 chest = body->feet + glm::vec3(0.0f, body->height * 0.65f, 0.0f);
                spawnSparks(chest, 8, 2.0f, 1.0f, glm::vec3(0.8f, 0.1f, 0.1f));
                if (m_others.jolt) {
                    m_others.jolt(static_cast<PlayerId>(hurt->player), chest - hurt->from, std::clamp(hurt->amount / 0.35f, 0.4f, 2.5f), false);
                }
                m_audio.play("player.hurt", SoundGroup::Player, 0.8f, chest);
            }
            continue;
        }
        if (const auto* downed = std::get_if<PlayerDowned>(&event)) {
            const PlayerBody* body = m_play.bodyOf(downed->player);
            m_audio.play("player.hurt", SoundGroup::Player, 1.0f,
                         downed->player == m_local || !body ? std::nullopt : std::optional<glm::vec3>(body->feet), 0.6f);
        } else if (const auto* taken = std::get_if<MaterialPickedUp>(&event); taken && taken->player != m_local) {
            m_audio.play("pickup", SoundGroup::World, 1.0f, taken->position);
        } else if (std::holds_alternative<PlayerRevived>(event)) {
            m_audio.play("pickup", SoundGroup::Player, 1.0f, std::nullopt, 0.8f);
        }
        if (const auto* mine = std::get_if<ShotFired>(&event); mine && mine->player == m_local && mine->last) {
            m_audio.play("revolver.last", SoundGroup::Weapon, 1.0f, std::nullopt, 1.0f, 0.05f);
        }
        const auto* takenHere = std::get_if<MaterialPickedUp>(&event);
        if (!takenHere || takenHere->player == m_local) {
            m_audio.onEvent(event);
        }

        if (const auto* mine = std::get_if<ProjectileImpact>(&event);
            mine && mine->shooter == m_local && (mine->surface == Surface::Ghost || mine->surface == Surface::Player)) {
            m_hitMark = {1.0f, HitMark::Kind::Hit, mine->surface == Surface::Ghost ? mine->body : 0u};
            m_audio.play("hit.mark", SoundGroup::Player, 0.6f);
        } else if (const auto* hurtIt = std::get_if<GhostHurt>(&event); hurtIt && m_hitMark.age01 > 0.9f && hurtIt->id == m_hitMark.ghost) {
            if (hurtIt->killed) {
                m_hitMark.kind = HitMark::Kind::Kill;
                m_audio.play("hit.kill", SoundGroup::Player, 0.7f);
            } else if (hurtIt->amount <= 0.001f) {
                m_hitMark.kind = HitMark::Kind::NoEffect;
            }
        } else if (const auto* downedBy = std::get_if<PlayerDowned>(&event); downedBy && downedBy->by == m_local && downedBy->player != m_local) {
            m_hitMark = {1.0f, HitMark::Kind::Kill, 0u};
            m_audio.play("hit.kill", SoundGroup::Player, 0.7f);
        }
        if (const auto* fired = std::get_if<ShotFired>(&event)) {
            const ElementDef& firedDef = m_ammo.elements[fired->round.element];

            m_flashElectric = firedDef.hitscanRange > 0.0f || firedDef.self != SelfEffect::None || firedDef.breathRange > 0.0f;
            const float side = unit(m_fxRng) < 0.5f ? -1.0f : 1.0f;
            m_viewmodel.kick(1.0f, side);
            if (true) {
                const CameraRecoilTuning& r = m_cameraRecoil;
                m_pendingPitch += glm::radians(r.permanentDeg);
                m_cameraKick.velocity += glm::vec3(r.kickImpulse, r.yawImpulse * (unit(m_fxRng) * 2.0f - 1.0f),
                                                   r.rollImpulse * (unit(m_fxRng) * 2.0f - 1.0f));
                m_fovPunch.velocity.x += r.fovPunch;
                m_shakeTime = r.shakeTime;
                m_shakeSeed = unit(m_fxRng) * 100.0f;
            }
            m_flashTime = m_effects.flashTime;
            m_flashSeed = unit(m_fxRng) * 6.28f;

            const glm::vec3 forward = m_lastGunForward;
            for (int i = 0; i < 2; ++i) {
                const glm::vec3 jitter{unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f};
                SmokePuff puff;
                puff.cloud.id = m_nextSmokeId++;
                puff.cloud.ball = true;
                puff.cloud.age = 0.4f;
                puff.cloud.center = m_lastMuzzleWorld + forward * (0.25f + 0.3f * static_cast<float>(i));
                puff.cloud.radius = 0.3f + 0.12f * static_cast<float>(i);
                puff.cloud.height = 0.24f + 0.08f * static_cast<float>(i);
                puff.cloud.lifetime = 0.4f + m_effects.smokeLife * (1.3f + 0.4f * unit(m_fxRng));
                puff.velocity = forward * (1.6f + 2.2f * static_cast<float>(i)) + jitter * 0.3f + m_play.upAt(m_lastMuzzleWorld) * 0.1f;
                m_smoke.push_back(puff);
            }
            while (m_smoke.size() > 12) {
                m_smoke.erase(m_smoke.begin());
            }
            m_frameFlash = std::max(m_frameFlash, 0.06f);
        } else if (std::holds_alternative<DryFired>(event)) {
            m_viewmodel.kick(0.04f, 0.0f);
        } else if (const auto* ejected = std::get_if<ChambersEjected>(&event)) {
            for (int k = 0; k < kChamberCount; ++k) {
                if (ejected->contents[static_cast<std::size_t>(k)].state != ChamberState::Spent) {
                    continue;
                }
                Debris d;
                d.spawn = m_lastChamberTransforms[static_cast<std::size_t>(k)];
                d.element = ejected->contents[static_cast<std::size_t>(k)].round.element;
                d.spawnCenter = glm::vec3(d.spawn * glm::vec4(m_revolver.roundCenter(), 1.0f));
                d.position = d.spawnCenter;
                d.viewmodelLayer = true;

                const glm::vec3 out = glm::normalize(glm::vec3(d.spawn * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                const glm::vec3 jitter{unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f};
                d.velocity = m_chamberVelocities[static_cast<std::size_t>(k)] + out * (0.05f + unit(m_fxRng) * 0.08f) +
                             jitter * 0.04f;
                d.spin = jitter * 7.0f;
                m_debris.push_back(d);
            }

            m_viewmodel.kick(0.015f, 0.0f);
            while (m_debris.size() > kMaxDebris) {
                m_debris.erase(m_debris.begin());
            }
        } else if (std::holds_alternative<SpeedloaderUsed>(event)) {
            m_viewmodel.kick(0.08f, 0.0f);
        } else if (std::holds_alternative<RoundLoaded>(event)) {
            m_viewmodel.kick(0.02f, 0.0f);
        } else if (const auto* playerHit = std::get_if<ProjectileImpact>(&event); playerHit && playerHit->surface == Surface::Player) {
            if ((playerHit->body & kHeadHit) != 0) {
                if (m_others.jolt) {
                    m_others.jolt(static_cast<PlayerId>(playerHit->body & 0xFFu), playerHit->direction, 1.0f, true);
                }
            }
        } else if (const auto* impact = std::get_if<ProjectileImpact>(&event);
                   impact && impact->surface != Surface::Ghost) {
            m_impactFlashes.push_back({impact->point + impact->normal * 0.02f, impact->surface, 0.0f,
                                       unit(m_fxRng) * 6.28f});

            const bool steel = impact->surface == Surface::Steel;
            const glm::vec3 dust = steel ? glm::vec3(0.55f, 0.55f, 0.55f) : glm::vec3(0.52f, 0.46f, 0.38f);
            const int count = steel ? 2 : 4;
            for (int i = 0; i < count; ++i) {
                const glm::vec3 jitter{unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f, unit(m_fxRng) - 0.5f};
                const float s = m_effects.impactScale;
                spawnPuff(impact->point + impact->normal * 0.04f,
                          impact->normal * (0.6f + unit(m_fxRng) * 1.6f) + jitter * 0.8f, dust,
                          1.0f + unit(m_fxRng) * 1.2f, 0.06f * s, (0.45f + unit(m_fxRng) * 0.4f) * s,
                          steel ? 0.25f : 0.45f);
            }
            if (!impact->ricochet || impact->surface != Surface::Steel) {
                if (impact->dynamicBody) {
                    m_marks.push_back({impact->bodyPoint, impact->bodyNormal, impact->surface, unit(m_fxRng) * 6.28f,
                                       impact->body, true});
                } else {
                    m_marks.push_back({impact->point, impact->normal, impact->surface, unit(m_fxRng) * 6.28f});
                }
                if (m_marks.size() > kMaxMarks) {
                    m_marks.pop_front();
                }
            }

            const ElementDef& def = m_ammo.elements[impact->element];
            const TrailKind burst = impact->ricochet ? TrailKind::None : def.trail;
            if (burst == TrailKind::Embers) {
                m_fire.burst(impact->point + impact->normal * 0.05f, 0.15f, 16, 1.8f);
            } else if (burst == TrailKind::FlamingWhirl) {
                startInferno(impact->point, impact->normal, 1.0f, !impact->dynamicBody);
                if (impact->dynamicBody) {
                    m_scorches.push_back({impact->bodyPoint + impact->bodyNormal * 0.004f, impact->bodyNormal, 0.2f,
                                          0.0f, unit(m_fxRng) * 10.0f, impact->body, true});
                }
            } else if (burst == TrailKind::Whirl) {
                ImpactFlash gust{impact->point + impact->normal * 0.1f, impact->surface, 0.0f, unit(m_fxRng) * 6.28f};
                gust.color = glm::vec3(0.55f, 0.82f, 1.0f);
                gust.sizeScale = 4.0f;
                gust.duration = 0.2f;
                gust.spikes = 0.2f;
                m_impactFlashes.push_back(gust);
                m_rings.push_back({impact->point + impact->normal * 0.05f, impact->normal, 2.5f, 0.38f, 0.0f, false,
                                   unit(m_fxRng) * 10.0f});
            }

            if (const auto head = m_trailHeads.find(impact->projectile); head != m_trailHeads.end()) {
                if (def.trail == TrailKind::Whirl || def.trail == TrailKind::FlamingWhirl) {
                    addTrail(head->second, impact->point, impact->element);
                }
                head->second = impact->point;
            }
        } else if (const auto* transformed = std::get_if<ProjectileTransformed>(&event)) {
            ImpactFlash catchFlash{transformed->point, Surface::Steel, 0.0f, unit(m_fxRng) * 6.28f};
            catchFlash.color = glm::vec3(1.0f, 0.6f, 0.25f);
            catchFlash.sizeScale = 4.5f;
            catchFlash.duration = 0.22f;
            catchFlash.spikes = 1.0f;
            m_impactFlashes.push_back(catchFlash);
            m_lightSpikes.push_back({transformed->point, glm::vec3(1.0f, 0.55f, 0.2f) * 25.0f, 0.0f, 0.25f});
            m_fire.burst(transformed->point, 0.35f, 50, 4.0f);
            spawnSparks(transformed->point, 40, 6.0f, 1.2f);
            m_rings.push_back({transformed->point, m_play.upAt(transformed->point), 1.4f, 0.3f, 0.0f, true,
                               unit(m_fxRng) * 10.0f});
        } else if (const auto* reacted = std::get_if<VolumeReacted>(&event)) {
            if (reacted->element == m_ammo.element("inferno")) {
                startInferno(reacted->point, m_play.upAt(reacted->point), 0.8f);
            } else {
                m_fire.burst(reacted->point, 0.45f, 60, 3.5f);
            }
        } else if (const auto* pressure = std::get_if<PressureBurst>(&event)) {
            ImpactFlash pop{pressure->center, Surface::Steel, 0.0f, unit(m_fxRng) * 6.28f};
            pop.color = glm::vec3(0.75f, 0.92f, 1.0f);
            pop.sizeScale = 7.0f;
            pop.duration = 0.12f;
            pop.spikes = 0.0f;
            m_impactFlashes.push_back(pop);
            const glm::mat3 basis = tornadoBasis(m_play.upAt(pressure->center));
            m_rings.push_back({pressure->center, basis[1], pressure->radius, 0.22f, 0.0f, false, unit(m_fxRng) * 10.0f});
            for (int i = 0; i < 16; ++i) {
                const float a = glm::two_pi<float>() * (static_cast<float>(i) + unit(m_fxRng)) / 16.0f;
                const glm::vec3 out = around(basis, a, 0.1f);
                spawnPuff(pressure->center, out * (5.0f + unit(m_fxRng) * 3.0f), glm::vec3(0.7f, 0.74f, 0.76f), 0.5f + unit(m_fxRng) * 0.3f, 0.1f, 0.7f, 0.35f);
            }
            const float closeBy = std::clamp(1.0f - glm::distance(pressure->center, m_world.position) / (pressure->radius * 2.5f), 0.0f, 1.0f);
            m_blastShake = std::max(m_blastShake, 2.0f * closeBy);
            m_fovPunch.velocity.x += 30.0f * closeBy;
        } else if (const auto* gust = std::get_if<GustBurst>(&event)) {
            m_ripples.push_back({gust->center, 0.0f, 0.4f, gust->radius, unit(m_fxRng) * 10.0f});
            const glm::mat3 basis = tornadoBasis(m_play.upAt(gust->center));
            for (int i = 0; i < 12; ++i) {
                const float a = glm::two_pi<float>() * static_cast<float>(i) / 12.0f + unit(m_fxRng) * 0.4f;
                const glm::vec3 out = around(basis, a, 0.15f + unit(m_fxRng) * 0.3f);
                spawnPuff(gust->center, out * (2.0f + unit(m_fxRng) * 2.5f), glm::vec3(0.6f, 0.62f, 0.62f),
                          0.9f + unit(m_fxRng) * 0.6f, 0.08f, 0.6f + unit(m_fxRng) * 0.4f, 0.3f);
            }
            for (Puff& p : m_puffs) {
                const glm::vec3 d = p.position - gust->center;
                const float distance = glm::length(d);
                if (distance < gust->radius && distance > 1e-4f) {
                    p.velocity += d / distance * gust->impulse * (1.0f - distance / gust->radius);
                }
            }
            for (Debris& d : m_debris) {
                const glm::vec3 offset = d.position - gust->center;
                const float distance = glm::length(offset);
                if (distance < gust->radius && distance > 1e-4f) {
                    d.velocity += glm::normalize(offset / distance + glm::vec3(0, 0.6f, 0)) * gust->impulse *
                                  (1.0f - distance / gust->radius);
                    d.resting = false;
                }
            }
            m_fire.push(gust->center, gust->radius, gust->impulse);
        } else if (const auto* faded = std::get_if<ProjectileFaded>(&event)) {
            if (faded->shooter != m_local) {
                continue;
            }

            ImpactFlash glint{faded->point, Surface::Steel, 0.0f, unit(m_fxRng) * 6.28f};
            glint.color = glm::vec3(0.55f, 0.72f, 1.0f);
            glint.sizeScale = 0.55f;
            glint.duration = 0.16f;
            glint.spikes = 1.0f;
            m_impactFlashes.push_back(glint);
            m_ripples.push_back({faded->point, 0.0f, 0.35f, 0.45f, unit(m_fxRng) * 10.0f});
        } else if (std::holds_alternative<PlayerHit>(event)) {
            const ElementId hurtBy = std::get<PlayerHit>(event).element;
            m_hitFlashColor = hurtBy == m_fogElement && m_fogParams ? glm::vec3(0.1f, 0.25f, 0.55f) : glm::vec3(0.3f, 0.03f, 0.4f);
            m_hitFlash = 1.0f;
            m_viewmodel.kick(1.4f, unit(m_fxRng) < 0.5f ? -1.0f : 1.0f);
            if (true) {
                const CameraRecoilTuning& r = m_cameraRecoil;
                m_cameraKick.velocity += glm::vec3(-r.kickImpulse * 1.5f, r.yawImpulse * 3.0f * (unit(m_fxRng) * 2.0f - 1.0f),
                                                   r.rollImpulse * 2.5f * (unit(m_fxRng) < 0.5f ? -1.0f : 1.0f));
                m_blastShake = std::max(m_blastShake, 2.5f);
            }
        } else if (const auto* ghostHurt = std::get_if<GhostHurt>(&event)) {
            const auto struck = std::find_if(m_wispClouds.begin(), m_wispClouds.end(),
                                             [&](const WispCloud& c) { return c.id == ghostHurt->id && c.dispersing <= 0.0f; });
            const bool unseen = struck != m_wispClouds.end() && struck->apparition;
            if (unseen) {
                struck->show = 1.0f;
            }
            const glm::vec3 pale = unseen ? glm::vec3(0.35f, 1.0f, 0.45f) : glm::vec3(0.6f, 1.0f, 0.85f);
            ImpactFlash flash{ghostHurt->point, Surface::Ghost, 0.0f, unit(m_fxRng) * 6.28f};
            flash.color = pale;
            flash.sizeScale = ghostHurt->killed ? 2.4f : 1.3f;
            flash.duration = 0.14f;
            flash.spikes = 1.0f;
            m_impactFlashes.push_back(flash);
            spawnSparks(ghostHurt->point, ghostHurt->killed ? 26 : 10, 3.0f, 1.0f, pale);
        } else if (const auto* threw = std::get_if<GhostThrew>(&event)) {
            spawnSparks(threw->from, 14, 3.0f, 1.0f, glm::vec3(0.35f, 1.0f, 0.45f));
            m_ripples.push_back({threw->from, 0.0f, 0.3f, 1.2f, unit(m_fxRng) * 10.0f});
        } else if (const auto* dodged = std::get_if<GhostDodged>(&event)) {
            for (int i = 0; i < 6; ++i) {
                spawnSparks(dodged->position + dodged->direction * (0.1f * static_cast<float>(i)), 1, 0.3f, 1.0f,
                            glm::vec3(0.5f, 1.0f, 0.8f));
                m_sparks.back().gravity = 0.0f;
                m_sparks.back().life = 0.3f;
            }
        } else if (const auto* burst = std::get_if<GhostBurst>(&event)) {
            const glm::vec3 hot{1.0f, 0.95f, 0.7f};
            ImpactFlash flash{burst->position, Surface::Ghost, 0.0f, unit(m_fxRng) * 6.28f};
            flash.color = hot;
            flash.sizeScale = 7.0f;
            flash.duration = 0.25f;
            flash.spikes = 1.0f;
            m_impactFlashes.push_back(flash);
            spawnSparks(burst->position, 70, 8.0f, 1.0f, hot);
            m_ripples.push_back({burst->position, 0.0f, 0.35f, burst->radius * 1.6f, unit(m_fxRng) * 10.0f});
            m_lightSpikes.push_back({burst->position, hot * 30.0f, 0.0f, 0.25f});
            m_frameFlash = std::max(m_frameFlash, 0.35f);
        } else if (const auto* died = std::get_if<GhostDied>(&event)) {
            if (!died->burst) {
                m_ripples.push_back({died->position, 0.0f, 0.3f, 0.9f, unit(m_fxRng) * 10.0f});
                for (int i = 0; i < 8; ++i) {
                    m_fire.emit(died->position, glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng), unit(m_fxRng) - 0.5f) * 2.0f,
                                0.3f, 0.5f, -0.9f, 2.5f, 0.5f);
                }
            }
        } else if (const auto* damaged = std::get_if<PlayerDamaged>(&event)) {
            m_hurtFlash = std::min(1.0f, m_hurtFlash + 0.5f + damaged->amount);
            m_blastShake = std::max(m_blastShake, 2.5f);
            m_cameraKick.velocity += glm::vec3(-m_cameraRecoil.kickImpulse * 1.2f, 0.0f,
                                               m_cameraRecoil.rollImpulse * 3.0f * (unit(m_fxRng) < 0.5f ? -1.0f : 1.0f));
            m_viewmodel.kick(1.0f, unit(m_fxRng) < 0.5f ? -1.0f : 1.0f);
        } else if (std::holds_alternative<PlayerDied>(event)) {
            m_frameFlash = 0.85f;
            m_hurtShown = 1.0f;
        } else if (const auto* taken = std::get_if<MaterialPickedUp>(&event)) {
            Absorb a;
            a.look = m_ammo.materials[taken->material].look;
            a.material = static_cast<MaterialId>(taken->material);
            a.player = taken->player;
            a.color = m_ammo.materials[taken->material].color;
            a.seed = unit(m_fxRng) * 6.28f;

            const bool hovers = a.look != MaterialDef::Look::Glow && a.look != MaterialDef::Look::SmallFire;
            a.from = taken->position + (hovers ? glm::vec3(0.0f, 0.16f, 0.0f) : glm::vec3(0.0f));
            a.position = a.from;
            a.target = absorbTarget(a.player);
            switch (a.look) {
            case MaterialDef::Look::StormOrb: a.life = 0.45f; break;
            case MaterialDef::Look::HazeBubble:
            case MaterialDef::Look::WindCloud: a.life = 1.2f; break;
            case MaterialDef::Look::ThreadKnot: a.life = 1.1f; break;
            case MaterialDef::Look::SmallFire:
                a.life = 0.9f;
                m_fire.burst(a.from + m_play.upAt(a.from) * 0.05f, 0.1f, 14, 1.4f);
                break;
            default: a.life = 0.7f; break;
            }
            m_absorbs.push_back(a);
        } else if (const auto* found = std::get_if<PlayerRevealed>(&event)) {
            RevealShot shot;
            float height = 1.6f;
            if (found->player != m_local && m_others.pose && m_others.pose(found->player, shot.pose, shot.headRadius, shot.color, height)) {
                shot.body = true;
                shot.heart = found->position + glm::vec3(0.0f, height * 0.6f, 0.0f);
                shot.seed = static_cast<float>(found->player) * 17.3f;
                shot.duration = found->duration;
                m_revealShots.push_back(shot);
            }
        } else if (const auto* revealed = std::get_if<GhostRevealed>(&event)) {
            RevealShot shot;
            shot.heart = revealed->position;
            shot.cloud = revealed->position;
            shot.radius = revealed->radius * kWispMistScale * 1.15f;
            if (const Ghost* seen = m_ghosts.find(revealed->id); seen && m_ghosts.def(*seen).invisible) {
                shot.apparition = true;
                shot.radius = revealed->radius * 1.8f;
            }
            shot.duration = revealed->duration;
            const auto cloud = std::find_if(m_wispClouds.begin(), m_wispClouds.end(),
                                            [&](const WispCloud& c) { return c.id == revealed->id && c.dispersing <= 0.0f; });
            if (cloud != m_wispClouds.end()) {
                shot.cloud = shot.apparition ? revealed->position : cloud->center;
                shot.tail = m_play.upAt(revealed->position) * 0.7f - (revealed->position - cloud->center) * 2.5f;
                shot.seed = cloud->seed;
                shot.phase = cloud->phase;
                shot.flow = cloud->flow;
            }
            m_revealShots.push_back(shot);
        } else if (const auto* steam = std::get_if<SteamExplosion>(&event)) {
            m_steam.push_back({steam->cloudCenter, steam->cloudRadius, steam->cloudHeight, 0.0f, steam->lifetime,
                               steam->strength, m_nextSmokeId++,
                               measureFogReach(steam->cloudCenter, steam->cloudRadius * 2.1f, steam->cloudHeight * 2.6f, [this](const glm::vec3& from, const glm::vec3& to) -> std::optional<float> {
                                   if (const auto hit = m_physics.raycast(from, to)) {
                                       return hit->fraction;
                                   }
                                   return std::nullopt;
                               })});
            const glm::vec3 white{1.0f, 0.97f, 0.92f};
            ImpactFlash flash{steam->center, Surface::Ground, 0.0f, unit(m_fxRng) * 6.28f};
            flash.color = white;
            flash.sizeScale = 12.0f * steam->strength;
            flash.duration = 0.18f;
            flash.spikes = 0.3f;
            m_impactFlashes.push_back(flash);
            m_ripples.push_back({steam->center, 0.0f, 0.45f, steam->radius * 1.5f, unit(m_fxRng) * 10.0f});
            const glm::mat3 steamBasis = tornadoBasis(m_play.upAt(steam->center));
            const glm::vec3 steamUp = steamBasis[1];
            m_rings.push_back({steam->center + steamUp * (glm::dot(steam->cloudCenter - steam->center, steamUp) + 0.05f), steamUp,
                               steam->radius * 1.2f, 0.4f, 0.0f, false, unit(m_fxRng) * 10.0f});
            m_lightSpikes.push_back({steam->center, glm::vec3(1.0f, 0.85f, 0.65f) * 30.0f * steam->strength, 0.0f, 0.3f});

            for (int i = 0; i < 26; ++i) {
                glm::vec3 out = steamBasis * glm::vec3(unit(m_fxRng) - 0.5f, unit(m_fxRng) * 0.6f, unit(m_fxRng) - 0.5f);
                out = glm::normalize(out + steamUp * 1e-3f);
                spawnPuff(steam->center + out * 0.5f, out * (5.0f + 7.0f * unit(m_fxRng)), glm::vec3(0.9f, 0.92f, 0.94f),
                          0.8f + 0.8f * unit(m_fxRng), 0.3f, 1.4f, 0.28f);
            }
            const glm::vec3 eye = m_lastMuzzleWorld;
            const float proximity = std::clamp(1.0f - glm::distance(eye, steam->center) / 30.0f, 0.0f, 1.0f);
            m_blastShake = std::max(m_blastShake, 4.0f * proximity * steam->strength);
            m_frameFlash = std::max(m_frameFlash, 0.45f * proximity * steam->strength);
            m_fovPunch.velocity.x += 60.0f * proximity;
        } else if (const auto* breath = std::get_if<BreathFired>(&event)) {
            m_breathFx.push_back({breath->origin, breath->direction, breath->range, breath->halfAngle, breath->duration});
            m_breathFx.back().mine = breath->player == m_local;
            m_blastShake = std::max(m_blastShake, 0.8f);
        } else if (const auto* charged = std::get_if<FogElectrified>(&event)) {
            const glm::vec3 heart = charged->center + glm::vec3(0.0f, charged->height * 0.4f, 0.0f);
            m_lightSpikes.push_back({heart + glm::vec3(0.0f, charged->height, 0.0f), glm::vec3(0.55f, 0.7f, 1.0f) * 10.0f, 0.0f,
                                     charged->duration});
            spawnSparks(heart, 30, 5.0f, 0.6f, glm::vec3(0.55f, 0.7f, 1.0f));
            m_frameFlash = std::max(m_frameFlash, 0.2f);
        } else if (const auto* cast = std::get_if<SelfCast>(&event)) {
            if (cast->player != m_local) {
                const ElementDef& theirs = m_ammo.elements[cast->element];
                if (cast->effect == SelfEffect::Reveal && m_client) {
                    RevealPulse pulse{cast->from, 0.0f, theirs.selfSpeed, theirs.selfDistance, theirs.selfDuration, {}};
                    pulse.owner = cast->player;
                    m_playMut.addReveal(pulse);
                }
                continue;
            }
            const glm::vec3 cold{0.55f, 0.7f, 1.0f};
            const glm::mat3 castBasis = tornadoBasis(m_play.upAt(cast->to));
            const glm::vec3 up = castBasis[1];
            if (cast->effect == SelfEffect::Reveal) {
                m_lightSpikes.push_back({cast->to + up * 0.6f, glm::vec3(1.0f, 0.55f, 0.2f) * 14.0f, 0.0f, 0.3f});
                m_frameFlash = std::max(m_frameFlash, 0.1f);
                m_fovPunch.velocity.x += 60.0f;
            } else if (cast->effect == SelfEffect::Haste) {
                m_rings.push_back({cast->to + up * 0.05f, up, 3.0f, 0.4f, 0.0f, false, unit(m_fxRng) * 10.0f});
                for (int i = 0; i < 10; ++i) {
                    const float a = glm::two_pi<float>() * (static_cast<float>(i) + unit(m_fxRng)) / 10.0f;
                    const glm::vec3 out = around(castBasis, a);
                    spawnPuff(cast->to + out * 0.5f + up * 0.1f, out * 4.0f + up * 0.4f, glm::vec3(0.6f, 0.62f, 0.6f),
                              0.7f + 0.4f * unit(m_fxRng), 0.12f, 0.6f, 0.22f);
                }
                m_fovPunch.velocity.x += 130.0f;
            } else if (cast->effect == SelfEffect::Shroud) {
                m_selfFx.shroudCast = 0.0f;

                for (int i = 0; i < 14; ++i) {
                    const float a = glm::two_pi<float>() * (static_cast<float>(i) + unit(m_fxRng)) / 14.0f;
                    const glm::vec3 out = around(castBasis, a);
                    spawnPuff(cast->to + out * 3.0f + up * (0.3f + 1.2f * unit(m_fxRng)), -out * 3.2f,
                              glm::vec3(0.36f, 0.5f, 0.54f), 0.9f + 0.4f * unit(m_fxRng), 0.5f, 0.9f, 0.14f);
                }
                m_fovPunch.velocity.x -= 60.0f;
            } else if (cast->effect == SelfEffect::Blink) {
                const LightningTuning& lt = m_lightningTuning;
                const glm::vec3 chest = up * 1.1f;

                if (glm::distance(cast->from, cast->to) > 0.3f) {
                    ActiveBolt bolt;
                    bolt.from = cast->from + chest;
                    bolt.to = cast->to + chest;
                    bolt.flashTimes = {0.0f, 0.05f, 0.11f};
                    bolt.width = 0.06f;
                    bolt.power = 1.3f;
                    bolt.light = lt.light;
                    BoltParams params;
                    params.levels = 5;
                    params.jaggedness = lt.jaggedness * 0.7f;
                    params.branches = 4;
                    params.branchLength = 0.25f;
                    addBolt(std::move(bolt), params);
                }

                spawnSparks(cast->from + chest, 40, 5.0f, 1.2f, cold);
                if (const auto ground = m_physics.raycast(cast->from + up * 0.5f, cast->from - up * 0.3f)) {
                    m_scorches.push_back({ground->point + ground->normal * 0.008f, ground->normal, 0.35f, 0.0f, unit(m_fxRng) * 10.0f});
                }

                for (int i = 0; i < 9; ++i) {
                    glm::vec3 out{unit(m_fxRng) - 0.5f, unit(m_fxRng) * 0.7f - 0.3f, unit(m_fxRng) - 0.5f};
                    out = glm::normalize(out + glm::vec3(0.0f, 1e-3f, 0.0f));
                    ActiveBolt arc;
                    arc.from = cast->to + chest + out * 0.45f;
                    arc.to = cast->to + chest + out * (1.4f + 1.4f * unit(m_fxRng));
                    const float start = 0.05f * unit(m_fxRng);
                    arc.flashTimes = {start, start + 0.05f + 0.04f * unit(m_fxRng)};
                    arc.width = 0.02f;
                    arc.power = 1.0f;
                    BoltParams params;
                    params.levels = 4;
                    params.jaggedness = 0.28f;
                    params.branches = 2;
                    addBolt(std::move(arc), params);
                }

                const std::size_t firstSpark = m_sparks.size();
                spawnSparks(cast->to + chest, 50, 6.0f, 1.0f, cold);
                for (std::size_t i = std::min(firstSpark, m_sparks.size()); i < m_sparks.size(); ++i) {
                    Spark& spark = m_sparks[i];
                    const float sparkSpeed = glm::length(spark.velocity);
                    if (sparkSpeed > 1e-3f) {
                        spark.position += spark.velocity / sparkSpeed * 1.1f;
                    }
                    spark.size *= 0.45f;
                }
                if (const auto ground = m_physics.raycast(cast->to + up * 0.5f, cast->to - up * 0.6f)) {
                    m_rings.push_back({ground->point + ground->normal * 0.05f, ground->normal, 2.2f, 0.3f, 0.0f, false,
                                       unit(m_fxRng) * 10.0f});
                }
                m_lightSpikes.push_back({cast->to + chest, cold * lt.light * 1.5f, 0.0f, 0.2f});
                m_frameFlash = std::max(m_frameFlash, 0.5f);
                m_fovPunch.value.x -= 14.0f;
                m_cameraKick.velocity.x += m_cameraRecoil.kickImpulse * 0.6f;
                m_blastShake = std::max(m_blastShake, 1.2f);
                m_selfFx.blinkRush = 1.0f;
                m_selfFx.blinkGlow = 1.0f;
                m_selfFx.blinkDuration = std::max(cast->duration, 0.2f);
            }
        } else if (const auto* shotBolt = std::get_if<LightningBolt>(&event)) {
            const LightningTuning& lt = m_lightningTuning;
            ActiveBolt bolt;
            const bool mine = shotBolt->player == m_local;
            bolt.from = (mine && true) ? m_lastMuzzleWorld : shotBolt->from;
            bolt.fromMuzzle = mine;
            bolt.to = shotBolt->to;
            bolt.hold = shotBolt->hold;
            float at = 0.0f;
            for (int k = 0; k < std::max(lt.restrikes, 1); ++k) {
                bolt.flashTimes.push_back(at);
                at += 0.045f + 0.035f * unit(m_fxRng);
            }
            bolt.width = lt.gunWidth;
            bolt.frameFlash = lt.gunFlash;
            bolt.light = lt.light * 0.5f;
            BoltParams params;
            const float length = glm::distance(bolt.from, bolt.to);
            params.levels = std::clamp(static_cast<int>(std::log2(std::max(length, 0.5f) / 0.15f)), 3, 7);
            params.jaggedness = lt.jaggedness * 0.45f;
            params.straightStart = 0.35f;
            params.branches = 3;
            params.branchLength = 0.1f;
            addBolt(std::move(bolt), params);
            spawnSparks(shotBolt->to, 18, 4.0f, 1.0f, glm::vec3(0.55f, 0.7f, 1.0f));
        } else if (const auto* arcCharged = std::get_if<BallArcCharged>(&event)) {
            m_arcMarks.push_back({arcCharged->to, arcCharged->delay, 0.0f, unit(m_fxRng) * 10.0f});
        } else if (const auto* ballArc = std::get_if<BallArc>(&event)) {
            ActiveBolt bolt;
            bolt.from = ballArc->from;
            bolt.to = ballArc->to;
            bolt.flashTimes = {0.0f, 0.05f + 0.03f * unit(m_fxRng)};
            bolt.width = 0.022f;
            bolt.light = 0.7f;
            BoltParams params;
            params.levels = 5;
            params.jaggedness = 0.22f;
            params.branches = 2;
            params.branchLength = 0.25f;
            addBolt(std::move(bolt), params);
            spawnSparks(ballArc->to, 12, 3.0f, 1.0f, glm::vec3(0.7f, 0.55f, 1.0f));
        } else if (const auto* wormUp = std::get_if<NecromiteEmerged>(&event)) {
            spawnSparks(wormUp->position, 8, 1.6f, 1.0f, glm::vec3(0.16f, 0.12f, 0.08f));
            for (std::size_t k = m_sparks.size() - 8; k < m_sparks.size(); ++k) {
                m_sparks[k].coolTo = glm::vec3(0.06f, 0.05f, 0.04f);
                m_sparks[k].gravity = 1.0f;
            }
        } else if (const auto* krakaBurst = std::get_if<KrakaBurst>(&event)) {
            if (const auto flock = m_flocks.find(krakaBurst->id); flock != m_flocks.end()) {
                flock->second.burst(krakaBurst->position);
            }
            m_blastShake = std::max(m_blastShake, 1.2f * std::max(0.0f, 1.0f - glm::distance(krakaBurst->position, m_world.position) / 8.0f));
        } else if (const auto* merged = std::get_if<KrakaMerged>(&event)) {
            if (const auto from = m_flocks.find(merged->from); from != m_flocks.end()) {
                m_flocks[merged->into].takeIn(from->second);
                m_flocks.erase(merged->from);
            }
        } else if (const auto* split = std::get_if<KrakaSplit>(&event)) {
            if (const auto whole = m_flocks.find(split->id); whole != m_flocks.end()) {
                ShardFlock half = whole->second.divide();
                m_flocks[split->half] = std::move(half);
            }
        } else if (const auto* thrash = std::get_if<MimicThrash>(&event)) {
            MimicShown& shown = m_mimics[thrash->id];
            shown.lashAge = 0.0f;
            shown.lashAt = thrash->toward;
        } else if (const auto* folded = std::get_if<MimicConcealed>(&event)) {
            (void)folded;
        } else if (const auto* unmasked = std::get_if<MimicRevealed>(&event)) {
            spawnSparks(unmasked->position, 10, 3.0f, 1.0f, glm::vec3(0.5f, 0.01f, 0.01f));
            for (std::size_t k = m_sparks.size() - 10; k < m_sparks.size(); ++k) {
                m_sparks[k].coolTo = glm::vec3(0.15f, 0.0f, 0.0f);
                m_sparks[k].size *= 0.6f;
            }
        } else if (const auto* hop = std::get_if<BallHopped>(&event)) {
            ActiveBolt bolt;
            bolt.from = hop->from;
            bolt.to = hop->to;
            bolt.flashTimes = {0.0f, 0.04f, 0.09f};
            bolt.width = 0.04f;
            bolt.light = 1.2f;
            BoltParams params;
            params.levels = 6;
            params.jaggedness = 0.18f;
            params.branches = 3;
            params.branchLength = 0.2f;
            addBolt(std::move(bolt), params);
            spawnSparks(hop->from, 10, 3.5f, 0.5f, glm::vec3(0.75f, 0.6f, 1.0f));
            spawnSparks(hop->to, 14, 3.5f, 0.5f, glm::vec3(0.75f, 0.6f, 1.0f));
        } else if (const auto* called = std::get_if<StrikeCalled>(&event)) {
            const LightningTuning& lt = m_lightningTuning;
            m_clouds.push_back({called->id, called->cloud, called->point, 0.0f, called->delay});
            ActiveBolt bolt;
            bolt.from = called->cloud;
            bolt.to = called->point;
            bolt.strikeId = called->id;
            bolt.struck = false;
            bolt.leaderStart = std::max(called->delay - lt.leaderTime, 0.0f);
            float at = 0.0f;
            for (int k = 0; k <= lt.restrikes; ++k) {
                bolt.flashTimes.push_back(at);
                at += 0.05f + 0.04f * unit(m_fxRng);
            }
            bolt.width = lt.strikeWidth;
            bolt.power = 1.6f;
            bolt.frameFlash = lt.strikeFlash;
            bolt.light = lt.light * 2.0f;
            BoltParams params;
            params.jaggedness = lt.jaggedness;
            params.branches = 6;
            params.branchLength = 0.38f;
            addBolt(std::move(bolt), params);
        } else if (const auto* strike = std::get_if<LightningStrike>(&event)) {
            const LightningTuning& lt = m_lightningTuning;
            for (ActiveBolt& bolt : m_bolts) {
                if (bolt.strikeId != strike->id || bolt.struck) {
                    continue;
                }
                bolt.struck = true;
                if (glm::distance(bolt.to, strike->point) > 0.1f) {
                    BoltParams params;
                    params.jaggedness = lt.jaggedness;
                    params.branches = 6;
                    params.branchLength = 0.38f;
                    bolt.to = strike->point;
                    bolt.bolt = buildBolt(bolt.from, bolt.to, bolt.seed, params);
                }
            }
            for (StormCloud& cloud : m_clouds) {
                cloud.struck = cloud.struck || cloud.id == strike->id;
            }
            const glm::vec3 at = strike->point + strike->normal * 0.05f;
            ImpactFlash flash{at + strike->normal * 0.3f, Surface::Steel, 0.0f, unit(m_fxRng) * 6.28f};
            flash.color = glm::vec3(0.7f, 0.8f, 1.0f);
            flash.sizeScale = 9.0f;
            flash.duration = 0.22f;
            flash.spikes = 1.0f;
            m_impactFlashes.push_back(flash);
            spawnSparks(at, 110, 11.0f, 1.6f, glm::vec3(0.55f, 0.7f, 1.0f));
            m_rings.push_back({at, strike->normal, strike->radius, 0.3f, 0.0f, false, unit(m_fxRng) * 10.0f});
            m_scorches.push_back({strike->point + strike->normal * 0.008f, strike->normal, 0.9f, 0.0f, unit(m_fxRng) * 10.0f});

            const glm::mat4 basis = basisFacing(strike->normal);
            for (int i = 0; i < 8; ++i) {
                const float a = glm::two_pi<float>() * (static_cast<float>(i) + unit(m_fxRng)) / 8.0f;
                const glm::vec3 out = glm::vec3(basis[0]) * std::cos(a) + glm::vec3(basis[1]) * std::sin(a);
                ActiveBolt arc;
                arc.from = at;
                arc.to = at + out * (1.0f + 1.6f * unit(m_fxRng));
                const float start = 0.02f + 0.16f * unit(m_fxRng);
                arc.flashTimes = {start, start + 0.05f + 0.04f * unit(m_fxRng)};
                arc.width = 0.018f;
                arc.power = 0.8f;
                BoltParams params;
                params.levels = 4;
                params.jaggedness = 0.28f;
                params.branches = 1;
                addBolt(std::move(arc), params);
            }
            const glm::vec3 eye = m_lastMuzzleWorld;
            const float proximity = std::clamp(1.0f - glm::distance(eye, at) / 25.0f, 0.0f, 1.0f);
            m_blastShake = std::max(m_blastShake, 3.5f * proximity);
            m_fovPunch.velocity.x += 70.0f * proximity;
        } else if (const auto* cone = std::get_if<FlameCone>(&event)) {
            m_jets.push_back({cone->origin, cone->direction, cone->length, 0.0f});
        }
    }
}

void PlayView::drawRadial() {
    if (!m_radialOpen) {
        return;
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    const ImVec2 center{size.x * 0.5f, size.y * 0.5f};
    const float radius = 110.0f;

    draw->AddCircleFilled(center, radius + 50.0f, ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.4f}), 48);
    if (m_radialOptions.empty()) {
        const char* text = "no special rounds";
        const ImVec2 t = ImGui::CalcTextSize(text);
        draw->AddText({center.x - t.x * 0.5f, center.y - t.y * 0.5f}, ImGui::GetColorU32({1, 1, 1, 0.7f}), text);
        return;
    }
    const float step = glm::two_pi<float>() / static_cast<float>(m_radialOptions.size());
    for (std::size_t i = 0; i < m_radialOptions.size(); ++i) {
        const ElementId id = m_radialOptions[i];
        const ElementDef& def = m_ammo.elements[id];
        const bool chosen = static_cast<int>(i) == m_radialChoice;
        const float angle = step * static_cast<float>(i);
        const ImVec2 p{center.x + std::sin(angle) * radius, center.y - std::cos(angle) * radius};
        const glm::vec3 c = glm::min(def.glow + 0.15f, glm::vec3(1.0f));
        const float r = chosen ? 34.0f : 26.0f;
        draw->AddCircleFilled(p, r, ImGui::GetColorU32({c.r * 0.35f, c.g * 0.35f, c.b * 0.35f, 0.9f}), 32);
        draw->AddCircle(p, r, ImGui::GetColorU32({c.r, c.g, c.b, chosen ? 1.0f : 0.6f}), 32, chosen ? 3.0f : 1.5f);
        char label[64];
        std::snprintf(label, sizeof(label), "%s\n  x%d", def.display.c_str(), m_frame.pouch ? m_frame.pouch->count(id) : 0);
        const ImVec2 t = ImGui::CalcTextSize(label);
        draw->AddText({p.x - t.x * 0.5f, p.y - t.y * 0.5f}, ImGui::GetColorU32({1, 1, 1, 1}), label);
    }
    const glm::vec2 f = m_flick / kFlickMax * (radius * 0.6f);
    draw->AddLine(center, {center.x + f.x, center.y + f.y}, ImGui::GetColorU32({1, 1, 1, 0.8f}), 2.0f);
    draw->AddCircleFilled({center.x + f.x, center.y + f.y}, 4.0f, ImGui::GetColorU32({1, 1, 1, 0.9f}));
}

void PlayView::drawPlayersHud(const HudFrame& hud) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    auto centered = [&](float y, const char* text, const glm::vec4& color) {
        const ImVec2 extent = ImGui::CalcTextSize(text);
        draw->AddText({(size.x - extent.x) * 0.5f, y}, ImGui::GetColorU32({color.r, color.g, color.b, color.a}), text);
    };
    auto bar = [&](float y, float progress) {
        const float width = 220.0f;
        const ImVec2 a{(size.x - width) * 0.5f, y};
        draw->AddRectFilled(a, {a.x + width, a.y + 8.0f}, ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.5f}));
        draw->AddRectFilled(a, {a.x + width * std::clamp(progress, 0.0f, 1.0f), a.y + 8.0f}, ImGui::GetColorU32({0.75f, 0.95f, 0.8f, 0.9f}));
    };

    for (const HudPlayer& other : hud.others) {
        if (hud.rules->arena) {
            break;
        }
        const RosterEntry* entry = hud.roster->find(other.id);
        if (other.shroudFade >= 0.999f || (entry && (entry->zombie || entry->possessedBy != kNoPlayer))) {
            continue;
        }
        const glm::vec4 clip = hud.viewProj * glm::vec4(other.head, 1.0f);
        if (clip.w <= 0.1f) {
            continue;
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        const ImVec2 at{(ndc.x * 0.5f + 0.5f) * size.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y};
        char label[96];
        std::snprintf(label, sizeof(label), "%s%s", other.name.c_str(), entry && entry->downed ? " (down)" : "");
        const ImVec2 extent = ImGui::CalcTextSize(label);
        draw->AddText({at.x - extent.x * 0.5f, at.y - extent.y}, ImGui::GetColorU32({other.color.r, other.color.g, other.color.b, 0.9f * (1.0f - other.shroudFade)}), label);
        if (entry && !entry->downed) {
            draw->AddRectFilled({at.x - 20.0f, at.y + 2.0f}, {at.x - 20.0f + 40.0f * entry->health, at.y + 5.0f},
                                ImGui::GetColorU32({0.8f, 0.3f, 0.3f, 0.8f}));
        }
    }

    if (const RosterEntry* mine = hud.roster->find(m_local); mine && mine->downed) {
        if (mine->possessedBy != kNoPlayer) {
            centered(size.y * 0.42f, "Something is wearing your body", {1.0f, 0.5f, 0.45f, 1.0f});
            centered(size.y * 0.42f + 22.0f, "Your friends must put it down", {1.0f, 1.0f, 1.0f, 0.85f});
            return;
        }
        centered(size.y * 0.42f, "You are down", {1.0f, 0.5f, 0.45f, 1.0f});
        if (hud.rules->arena) {
            return;
        }
        if (mine->reviver != kNoPlayer) {
            centered(size.y * 0.42f + 22.0f, "A teammate is getting you up", {1.0f, 1.0f, 1.0f, 0.85f});
            bar(size.y * 0.42f + 46.0f, mine->revive);
        } else {
            centered(size.y * 0.42f + 22.0f, "A teammate can revive you", {1.0f, 1.0f, 1.0f, 0.7f});
        }
        return;
    }
    if (hud.rules->arena) {
        return;
    }
    for (const HudPlayer& other : hud.others) {
        const RosterEntry* entry = hud.roster->find(other.id);
        if (!entry || !entry->downed || glm::distance(other.feet, m_world.position) > hud.rules->reviveRange) {
            continue;
        }
        char prompt[96];
        std::snprintf(prompt, sizeof(prompt), "Hold E to revive %s", other.name.c_str());
        centered(size.y * 0.62f, prompt, {1.0f, 1.0f, 1.0f, 0.9f});
        if (entry->reviver == m_local) {
            bar(size.y * 0.62f + 24.0f, entry->revive);
        }
        break;
    }
}

void PlayView::drawArenaHud(const HudFrame& hud) {
    if (!hud.rules->arena) {
        return;
    }
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    std::vector<ScoreLine> lines = hud.score;
    std::stable_sort(lines.begin(), lines.end(), [](const ScoreLine& a, const ScoreLine& b) { return a.kills > b.kills; });
    float y = 14.0f;
    for (const ScoreLine& line : lines) {
        char text[64];
        std::snprintf(text, sizeof(text), "%s   %d", line.name.c_str(), line.kills);
        const ImVec2 extent = ImGui::CalcTextSize(text);
        draw->AddText({(size.x - extent.x) * 0.5f, y}, ImGui::GetColorU32({line.color.r, line.color.g, line.color.b, 0.9f}), text);
        y += extent.y + 2.0f;
    }
    if (const RosterEntry* mine = hud.roster->find(m_local); mine && mine->downed) {
        char text[64];
        std::snprintf(text, sizeof(text), "Back in %.0f", static_cast<double>(std::ceil(std::max(hud.rules->arenaRespawn - mine->downTime, 0.0f))));
        const ImVec2 extent = ImGui::CalcTextSize(text);
        draw->AddText({(size.x - extent.x) * 0.5f, size.y * 0.42f + 22.0f}, ImGui::GetColorU32({1.0f, 1.0f, 1.0f, 0.85f}), text);
    }
}

void PlayView::drawHud(const HudFrame& hud) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    if (m_hitFlash > 0.01f) {
        ImGui::GetBackgroundDrawList()->AddRectFilled({0.0f, 0.0f}, size,
                                                      ImGui::GetColorU32({m_hitFlashColor.r, m_hitFlashColor.g, m_hitFlashColor.b, 0.55f * m_hitFlash}));
    }
    drawPlayersHud(hud);
    drawArenaHud(hud);
    if (!m_frame.armed || m_frame.downed) {
        return;
    }
    drawRadial();
    const ghost::game::PlayerState& ps = m_world;
    {
        float y = size.y - 46.0f;
        char line[48];
        if (ps.hasteTime > 0.0f) {
            std::snprintf(line, sizeof(line), "Tailwind  %.1f s", ps.hasteTime);
            draw->AddText({24.0f, y}, ImGui::GetColorU32({0.7f, 1.0f, 0.85f, 0.95f}), line);
            y -= 18.0f;
        }
        if (ps.shroudTime > 0.0f) {
            std::snprintf(line, sizeof(line), "Shroud  %.1f s", ps.shroudTime);
            draw->AddText({24.0f, y}, ImGui::GetColorU32({0.6f, 0.8f, 0.95f, 0.95f}), line);
        }
    }

    if (m_hitMark.age01 > 0.0f) {
        const bool kill = m_hitMark.kind == HitMark::Kind::Kill;
        const bool nothing = m_hitMark.kind == HitMark::Kind::NoEffect;
        const float a = m_hitMark.age01;
        const float inner = (kill ? 9.0f : 7.0f) + (1.0f - a) * 5.0f;
        const float outer = inner + (kill ? 12.0f : nothing ? 5.0f : 8.0f);
        const ImVec4 tint = kill ? ImVec4(1.0f, 0.2f, 0.15f, a) : nothing ? ImVec4(0.6f, 0.6f, 0.62f, a * 0.8f) : ImVec4(1.0f, 1.0f, 1.0f, a);
        const ImVec2 middle{size.x * 0.5f, size.y * 0.5f};
        for (int k = 0; k < 4; ++k) {
            const ImVec2 d{k % 2 == 0 ? 0.7071f : -0.7071f, k < 2 ? 0.7071f : -0.7071f};
            const ImVec2 from{middle.x + d.x * inner, middle.y + d.y * inner};
            const ImVec2 to{middle.x + d.x * outer, middle.y + d.y * outer};
            draw->AddLine(from, to, ImGui::GetColorU32({0.0f, 0.0f, 0.0f, a * 0.6f}), kill ? 5.0f : 4.0f);
            draw->AddLine(from, to, ImGui::GetColorU32(tint), kill ? 2.5f : 2.0f);
        }
    }

    if (ps.holstered && ps.holster >= 1.0f) {
        return;
    }
    if (m_frame.speedloaders) {
        for (int l = 0; l < kSpeedloadersCarried; ++l) {
            for (int k = 0; k < kSpeedloaderSlots; ++k) {
                const auto& slot = m_frame.speedloaders->loaders[static_cast<std::size_t>(l)].slots[static_cast<std::size_t>(k)];
                const ImVec2 p{size.x - 300.0f + 18.0f * static_cast<float>(k), size.y - 64.0f - 20.0f * static_cast<float>(l)};
                if (slot) {
                    const glm::vec3 c = slot->element == kPlainElement ? glm::vec3(0.8f, 0.7f, 0.45f)
                                                                        : glm::min(m_ammo.elements[slot->element].glow + 0.15f, glm::vec3(1.0f));
                    draw->AddCircleFilled(p, 6.0f, ImGui::GetColorU32({c.r, c.g, c.b, 0.95f}), 12);
                } else {
                    draw->AddCircle(p, 6.0f, ImGui::GetColorU32({1.0f, 1.0f, 1.0f, 0.25f}), 12, 1.0f);
                }
            }
        }
    }

    const MechanismState& s = gunState();
    const ImVec2 center{size.x - 90.0f, size.y - 90.0f};
    const float ring = 38.0f;
    draw->AddCircleFilled(center, ring + 22.0f, ImGui::GetColorU32({0, 0, 0, 0.35f}));
    for (int k = 0; k < kChamberCount; ++k) {
        const float angle = (static_cast<float>(k) - s.cylinderPosition()) * glm::two_pi<float>() / kChamberCount;
        const ImVec2 p{center.x + std::sin(angle) * ring, center.y - std::cos(angle) * ring};
        const Chamber& chamber = s.chambers[static_cast<std::size_t>(k)];
        const ChamberState state = chamber.state;
        if (state == ChamberState::Live) {
            const ElementDef& def = m_ammo.elements[chamber.round.element];
            const glm::vec3 tip = chamber.round.element == kPlainElement ? glm::vec3(0.55f, 0.55f, 0.57f)
                                                                        : glm::min(def.glow, glm::vec3(1.0f));
            draw->AddCircleFilled(p, 12.0f, ImGui::GetColorU32({0.85f, 0.65f, 0.3f, 1.0f}));
            draw->AddCircleFilled(p, 6.5f, ImGui::GetColorU32({tip.r, tip.g, tip.b, 1.0f}));
        } else if (state == ChamberState::Spent) {
            draw->AddCircleFilled(p, 12.0f, ImGui::GetColorU32({0.45f, 0.33f, 0.15f, 1.0f}));
            draw->AddCircleFilled(p, 5.0f, ImGui::GetColorU32({0.08f, 0.06f, 0.04f, 1.0f}));
        } else {
            draw->AddCircle(p, 12.0f, ImGui::GetColorU32({0.6f, 0.6f, 0.6f, 0.8f}), 0, 1.5f);
        }
        if (s.isClosed() && k == s.nextToFire()) {
            draw->AddCircle(p, 15.0f, ImGui::GetColorU32({1, 1, 1, 0.9f}), 0, 2.0f);
        }
        if (k == s.loadingChamber) {
            const float start = -glm::half_pi<float>();
            draw->PathArcTo(p, 15.0f, start, start + glm::two_pi<float>() * s.loadProgress, 24);
            draw->PathStroke(ImGui::GetColorU32({0.4f, 1.0f, 0.4f, 1.0f}), 0, 3.0f);
        }
    }
    if (s.cylinder == CylinderPhase::Open) {
        const float slot = glm::two_pi<float>() / kChamberCount;
        draw->AddCircle({center.x + std::sin(slot) * ring, center.y - std::cos(slot) * ring}, 17.5f,
                        ImGui::GetColorU32({1.0f, 0.8f, 0.3f, 0.95f}), 0, 2.5f);
    }
    const char* hammer = s.phase == HammerPhase::Cocked ? "COCKED" : (s.phase == HammerPhase::Cocking ? "cocking" : "");
    if (!s.isClosed()) {
        hammer = s.cylinder == CylinderPhase::Open ? "OPEN" : "...";
    }
    draw->AddText({center.x - 16.0f, center.y - 7.0f}, ImGui::GetColorU32({1, 1, 1, 0.9f}), hammer);

    if (m_frame.pouch) {
        const AmmoPouch& pouch = *m_frame.pouch;
        auto isRound = [this](std::size_t e) { return m_ammo.elements[static_cast<ElementId>(e)].explosionRadiusScale <= 0.0f; };
        std::size_t kinds = 0;
        for (std::size_t e = 0; e < m_ammo.elements.size(); ++e) {
            kinds += isRound(e) ? 1 : 0;
        }
        float lineY = center.y - ring - 30.0f - 16.0f * static_cast<float>(kinds);
        for (std::size_t e = 0; e < m_ammo.elements.size(); ++e) {
            if (!isRound(e)) {
                continue;
            }
            const auto id = static_cast<ElementId>(e);
            const ElementDef& def = m_ammo.elements[id];
            char line[64];
            std::snprintf(line, sizeof(line), "%-8s %d", def.display.c_str(), pouch.count(id));
            const glm::vec3 c = id == kPlainElement ? glm::vec3(0.9f) : glm::min(def.glow + 0.2f, glm::vec3(1.0f));
            draw->AddText({center.x - 40.0f, lineY}, ImGui::GetColorU32({c.r, c.g, c.b, pouch.count(id) ? 0.9f : 0.35f}), line);
            lineY += 16.0f;
        }
    }

    const char* prompt = nullptr;
    switch (s.cylinder) {
    case CylinderPhase::Closed:
        prompt = ps.holstered ? "H  draw" : "R  open cylinder   H  holster";
        break;
    case CylinderPhase::Open:
        if (s.ejectTime >= 0.0f) {
            prompt = "ejecting...";
        } else if (s.speedloadProgress >= 0.0f) {
            prompt = "speedloader...";
        } else if (s.loadingChamber >= 0) {
            prompt = "loading...   (R closes and stops)";
        } else {
            prompt = m_frame.pouch && m_frame.pouch->total() == 0
                         ? "F eject   wheel turn   R close   (pouch empty)"
                         : "F eject   wheel turn   LMB tap: plain   hold LMB + flick: special   G speedloader / quick-load   R/RMB close";
        }
        break;
    default:
        prompt = "";
        break;
    }
    if (m_frame.windowBlocked && !ps.holstered) {
        prompt = "aim out the window to fire";
    }
    const ImVec2 promptSize = ImGui::CalcTextSize(prompt);
    draw->AddText({(size.x - promptSize.x) * 0.5f, size.y - 60.0f}, ImGui::GetColorU32({1, 1, 1, 0.75f}), prompt);

    if (!ps.holstered) {
        const Chamber& up = s.chambers[static_cast<std::size_t>(s.nextToFire())];
        glm::vec3 c{0.6f};
        const char* name = "empty";
        if (up.state == ChamberState::Live) {
            const ElementDef& def = m_ammo.elements[up.round.element];
            name = def.display.c_str();
            c = up.round.element == kPlainElement ? glm::vec3(0.9f) : glm::min(def.glow + 0.2f, glm::vec3(1.0f));
        } else if (up.state == ChamberState::Spent) {
            name = "spent";
        }
        const float fontSize = ImGui::GetFontSize() * 2.0f;
        const ImVec2 nameSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name);
        const ImVec2 at{(size.x - nameSize.x) * 0.5f, size.y - 70.0f - nameSize.y};
        draw->AddRectFilled({at.x - 10.0f, at.y - 3.0f}, {at.x + nameSize.x + 10.0f, at.y + nameSize.y + 3.0f},
                            ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.45f}), 6.0f);
        draw->AddText(ImGui::GetFont(), fontSize, {at.x + 2.0f, at.y + 2.0f}, ImGui::GetColorU32({0.0f, 0.0f, 0.0f, 0.85f}), name);
        draw->AddText(ImGui::GetFont(), fontSize, at, ImGui::GetColorU32({c.r, c.g, c.b, 0.95f}), name);
    }

    const glm::vec3 feet = m_world.position;
    const bool roundNearby = std::any_of(m_dropped.begin(), m_dropped.end(), [&](const DroppedRound& d) {
        return d.resting && glm::length(glm::vec2(d.position.x - feet.x, d.position.z - feet.z)) < kPickupRadius;
    });
    const std::vector<ShownDrop> shown = shownDrops();
    const bool materialNearby = std::any_of(shown.begin(), shown.end(), [&](const ShownDrop& d) {
        return d.resting && glm::length(glm::vec2(d.position.x - feet.x, d.position.z - feet.z)) < kPickupRadius * 1.5f;
    });
    if (materialNearby && !m_radialOpen) {
        const char* take = hud.pickupProgress > 0.0f ? "taking it in..." : "hold E  take the material";
        const ImVec2 takeSize = ImGui::CalcTextSize(take);
        draw->AddText({(size.x - takeSize.x) * 0.5f, size.y * 0.5f + 40.0f}, ImGui::GetColorU32({1, 1, 1, 0.85f}), take);
    } else if (roundNearby && !m_radialOpen) {
        const char* pickup = hud.pickupProgress > 0.0f ? "picking up..." : "hold E  pick up round";
        const ImVec2 pickupSize = ImGui::CalcTextSize(pickup);
        draw->AddText({(size.x - pickupSize.x) * 0.5f, size.y * 0.5f + 40.0f}, ImGui::GetColorU32({1, 1, 1, 0.85f}), pickup);
    }
}

void PlayView::renderPropShadows(const std::vector<PropView>& props, const glm::mat4& lightViewProj, unsigned program) {
    if (props.empty() || program == 0) {
        return;
    }
    glUseProgram(program);
    glProgramUniformMatrix4fv(program, kShadowLightSlot, 1, GL_FALSE, &lightViewProj[0][0]);
    for (const PropView& prop : props) {
        const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), prop.center), prop.half);
        glProgramUniformMatrix4fv(program, kShadowModelSlot, 1, GL_FALSE, &model[0][0]);
        m_unitBox.draw();
    }
}
void PlayView::renderProps(const std::vector<PropView>& props, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                           const FrameLights& lights) {
    if (props.empty()) {
        return;
    }
    if (!m_surfaces) {
        m_surfaces = std::make_unique<MaterialLibrary>();
    }
    m_lightDir = lights.sunDirection;
    setFrameLights(m_lastMuzzleWorld);
    m_litShader.use();
    m_litShader.set("uViewProj", viewProj);
    m_litShader.set("uCameraPos", cameraPos);
    m_litShader.set("uGhost", 0.0f);
    for (const PropView& prop : props) {
        const glm::mat4 model = glm::scale(glm::translate(glm::mat4(1.0f), prop.center), prop.half);
        const TexturedMaterial* surface = prop.surface == Surface::Wood    ? &m_surfaces->planks
                                          : prop.surface == Surface::Steel ? &m_surfaces->paint
                                                                           : &m_surfaces->concrete;
        setSurface(m_litShader, prop.color, 0.0f, 0.8f);
        bindMaterial(m_litShader, surface, model, model, surface->tintFor(prop.color));
        drawWithModel(m_litShader, m_unitBox, model);
    }
    m_litShader.set("uUseMaps", 0);
}

void PlayView::renderCylinders(const std::vector<TintedCylinder>& cylinders, const glm::mat4& viewProj, const glm::vec3& cameraPos,
                               const FrameLights& lights) {
    if (cylinders.empty()) {
        return;
    }
    m_lightDir = lights.sunDirection;
    setFrameLights(m_lastMuzzleWorld);
    m_litShader.use();
    m_litShader.set("uViewProj", viewProj);
    m_litShader.set("uCameraPos", cameraPos);
    m_litShader.set("uGhost", 0.0f);
    m_litShader.set("uUseMaps", 0);
    for (const TintedCylinder& cylinder : cylinders) {
        setSurface(m_litShader, cylinder.color, 0.0f, 0.55f);
        drawWithModel(m_litShader, m_cylinder, cylinder.model);
    }
}

}
