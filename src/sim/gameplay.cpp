#include "sim/gameplay.h"

#include "engine/assets/asset_path.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <variant>

namespace ghost::game {

std::vector<float> ricochetScales(const AmmoData& ammo) {
    std::vector<float> scales;
    for (std::size_t i = 0; i < ammo.elements.size(); ++i) {
        scales.push_back(ammo.elements[static_cast<ElementId>(i)].ricochet);
    }
    return scales;
}
namespace {

constexpr int kStartingPouch = 12;
constexpr float kBurnRoundsPerSecond = 0.8f;
constexpr float kBurnColumnScale = 1.3f;
constexpr std::uint32_t kVolumeSource = 0x40000000u;
constexpr std::uint32_t kBreathSource = 0x80000000u;
constexpr std::uint32_t kHeadHit = 0x100u;
constexpr float kBreathRoundsPerSecond = 5.0f;
constexpr float kPickupRadius = 1.3f;
constexpr float kPickupTime = 0.35f;
constexpr int kArenaRounds = 30;
constexpr float kRoundRadius = 0.0061f;
constexpr float kMaterialRest = 0.25f;

glm::mat4 basisFacing(const glm::vec3& forward) {
    const glm::vec3 helper = std::abs(forward.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(helper, forward));
    const glm::vec3 y = glm::cross(forward, x);
    return glm::mat4(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(forward, 0.0f), glm::vec4(0, 0, 0, 1));
}

void hashBytes(std::uint64_t& h, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        h = (h ^ bytes[i]) * 1099511628211ull;
    }
}

}

Gameplay::Gameplay(Roster& roster, const PlayerRules& rules, engine::PhysicsWorld& physics)
    : m_roster(roster), m_rules(rules), m_physics(physics), m_ammo(loadAmmoData(engine::assetPath("data"))),
      m_ghostData(loadGhostData(engine::assetPath("data"), m_ammo)), m_materials(m_ammo.materials.size()) {
    for (std::size_t e = 0; e < m_ammo.elements.size(); ++e) {
        if (m_ammo.elements[static_cast<ElementId>(e)].fog.height > 0.0f) {
            m_fogElement = static_cast<ElementId>(e);
            m_fogParams = &m_ammo.elements[m_fogElement].fog;
        }
    }
}

float Gameplay::nextRandom() {
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 17;
    m_rng ^= m_rng << 5;
    return static_cast<float>(m_rng >> 8) / static_cast<float>(1u << 24);
}

void Gameplay::stockGun(PlayerGun& gun) const {
    gun.pouch = AmmoPouch(m_ammo.elements.size());
    gun.pouch.add(kPlainElement, kStartingPouch);
    for (const char* name : {"fire", "wind", "inferno", "lightning", "thunderstrike", "haze", "voodoo", "tailwind", "shroud",
                             "blink", "fog", "ghostfire", "firelight"}) {
        gun.pouch.add(m_ammo.element(name), 3);
    }
    gun.mechanism.loadAll(Round{});
}

void Gameplay::arenaKit(PlayerGun& gun) const {
    gun.pouch = AmmoPouch(m_ammo.elements.size());
    gun.pouch.add(kPlainElement, kArenaRounds);
    for (const char* name : {"fire", "wind", "inferno", "lightning", "thunderstrike", "haze", "voodoo", "tailwind", "shroud",
                             "blink", "fog", "ghostfire", "firelight"}) {
        gun.pouch.add(m_ammo.element(name), kArenaRounds);
    }
    gun.mechanism.loadAll(Round{});
    gun.pendingLoad.reset();
    gun.speedloaders = SpeedloaderRack{};
}

void Gameplay::tickGun(PlayerId owner, PlayerGun& gun, const GunCommand& command, bool holstered, bool downed, float dt,
                       EventList& events, std::vector<ShotFired>& shots, std::vector<ChambersEjected>& ejected) {
    GunCommand c = command;
    if (c.closeCylinder) {
        gun.closeLatched = true;
    }
    gun.turnLatched += c.turn;
    if (c.load) {
        gun.pendingLoad = Round{c.loadElement};
    }
    if (c.quickFill) {
        gun.quickFill = true;
        gun.quickFillElement = c.quickFillElement;
    }
    if (holstered) {
        gun.mechanism.snapClosed();
    }
    if (downed || holstered) {
        c.trigger = c.cock = false;
        c.toggleCylinder = c.eject = c.speedload = false;
        gun.closeLatched = false;
        gun.turnLatched = 0;
        gun.pendingLoad.reset();
        gun.quickFill = false;
    }

    MechanismInput weapon;
    weapon.triggerHeld = c.trigger;
    weapon.cock = c.cock;
    if (gun.pendingLoad) {
        const MechanismState& before = gun.mechanism.state();
        const int inserting = (before.loadingChamber >= 0 && before.loadingRound.element == gun.pendingLoad->element) ? 1 : 0;
        if (gun.pouch.count(gun.pendingLoad->element) > inserting) {
            weapon.load = true;
            weapon.roundToLoad = *gun.pendingLoad;
        } else {
            gun.pendingLoad.reset();
        }
    }
    const bool wasInserting = gun.mechanism.state().loadingChamber >= 0;
    weapon.toggleCylinder = c.toggleCylinder;
    weapon.closeCylinder = gun.closeLatched;
    weapon.eject = c.eject;
    if (c.speedload) {
        if (const int next = gun.speedloaders.nextFilled(); next >= 0) {
            weapon.speedload = gun.speedloaders.loaders[static_cast<std::size_t>(next)].slots;
        } else if (gun.mechanism.state().cylinder == CylinderPhase::Open) {
            gun.quickFill = true;
            gun.quickFillElement = kPlainElement;
        }
    }
    {
        const MechanismState& before = gun.mechanism.state();
        const bool anyEmpty = std::any_of(before.chambers.begin(), before.chambers.end(),
                                          [](const Chamber& ch) { return ch.state == ChamberState::Empty; });
        const int going = (before.loadingChamber >= 0 && before.loadingRound.element == gun.quickFillElement) ? 1 : 0;
        if (weapon.load || weapon.toggleCylinder || weapon.closeCylinder || weapon.eject || weapon.speedload ||
            before.cylinder != CylinderPhase::Open || before.ejectTime >= 0.0f || !anyEmpty ||
            gun.pouch.count(gun.quickFillElement) <= going) {
            gun.quickFill = false;
        }
        if (gun.quickFill) {
            Round plain;
            plain.element = gun.quickFillElement;
            weapon.load = true;
            weapon.roundToLoad = plain;
            weapon.quick = true;
        }
    }
    weapon.turn = std::clamp(gun.turnLatched, -1, 1);
    gun.turnLatched -= weapon.turn;

    const std::size_t firstEvent = events.size();
    gun.mechanism.tick(weapon, dt, events);
    const CylinderPhase cylinder = gun.mechanism.state().cylinder;
    if (cylinder == CylinderPhase::Closing || cylinder == CylinderPhase::Closed) {
        gun.closeLatched = false;
        gun.pendingLoad.reset();
    }
    if (weapon.load && !wasInserting) {
        gun.pendingLoad.reset();
    }
    for (std::size_t i = firstEvent; i < events.size(); ++i) {
        if (auto* mine = std::get_if<ShotFired>(&events[i])) {
            mine->player = owner;
            shots.push_back(*mine);
        } else if (const auto* loaded = std::get_if<RoundLoaded>(&events[i])) {
            gun.pouch.take(loaded->round.element);
        } else if (const auto* out = std::get_if<ChambersEjected>(&events[i])) {
            ejected.push_back(*out);
        } else if (std::holds_alternative<SpeedloaderUsed>(events[i])) {
            gun.speedloaders.useNext();
        }
    }
}

MechanismView Gameplay::viewOf(const PlayerGun& gun, float alpha, float time) const {
    const MechanismState& prev = gun.mechanism.previous();
    const MechanismState& cur = gun.mechanism.state();
    MechanismView view;
    view.hammer = glm::mix(prev.hammer, cur.hammer, alpha);
    view.trigger = glm::mix(prev.trigger, cur.trigger, alpha);
    float prevPos = prev.cylinderPosition();
    const float curPos = cur.cylinderPosition();
    if (curPos - prevPos < -kChamberCount * 0.5f) {
        prevPos -= kChamberCount;
    } else if (curPos - prevPos > kChamberCount * 0.5f) {
        prevPos += kChamberCount;
    }
    view.cylinder = glm::mix(prevPos, curPos, alpha);
    view.crane = glm::mix(prev.crane, cur.crane, alpha);
    view.ejector = glm::mix(prev.ejectorStroke(), cur.ejectorStroke(), alpha);
    view.loadingChamber = cur.loadingChamber;
    view.loadingElement = cur.loadingRound.element;
    if (cur.speedloadProgress >= 0.0f) {
        view.speedloadProgress =
            prev.speedloadProgress >= 0.0f ? glm::mix(prev.speedloadProgress, cur.speedloadProgress, alpha) : cur.speedloadProgress;
        view.speedloadFirst = cur.nextToFire();
        for (std::size_t i = 0; i < cur.speedloadRounds.size(); ++i) {
            view.speedloadElements[i] = cur.speedloadRounds[i] ? static_cast<int>(cur.speedloadRounds[i]->element) : -1;
        }
    }
    if (cur.cylinder == CylinderPhase::Open && cur.ejectTime < 0.0f) {
        view.highlightSlot = true;
        view.highlightPulse = 0.5f + 0.5f * std::sin(time * 6.0f);
    }
    view.loadProgress =
        cur.loadingChamber == prev.loadingChamber ? glm::mix(prev.loadProgress, cur.loadProgress, alpha) : cur.loadProgress;
    if (cur.hammer < prev.hammer && cur.phase == HammerPhase::Down && prev.phase == HammerPhase::Cocked) {
        view.hammer = cur.hammer;
    }
    for (int i = 0; i < kChamberCount; ++i) {
        view.chambers[static_cast<std::size_t>(i)] = cur.chambers[static_cast<std::size_t>(i)].state;
        view.elements[static_cast<std::size_t>(i)] = cur.chambers[static_cast<std::size_t>(i)].round.element;
    }
    return view;
}

void Gameplay::fireShot(PlayerId shooter, const glm::vec3& origin, const glm::vec3& forward, float spreadDeg,
                        const Round& fired, bool castHere, EventList& events, const glm::vec3& inherit) {
    const float angle = glm::radians(spreadDeg) * std::sqrt(nextRandom());
    const float around = glm::two_pi<float>() * nextRandom();
    const glm::mat4 basis = basisFacing(forward);
    const glm::vec3 offset = glm::vec3(basis[0]) * std::cos(around) + glm::vec3(basis[1]) * std::sin(around);
    const glm::vec3 direction = glm::normalize(forward + offset * std::tan(angle));
    const Round round = fired;
    if (round.element >= m_ammo.elements.size()) {
        return;
    }
    const ElementDef& def = m_ammo.elements[round.element];
    m_ghosts.reactToShot(origin, direction,
                         !def.stealthy && !def.ethereal && def.hitscanRange <= 0.0f && def.breathRange <= 0.0f &&
                             def.self == SelfEffect::None,
                         events);
    if (def.breathRange > 0.0f) {
        const glm::vec3 along = glm::normalize(forward);
        m_breaths.push_back({origin, along, def.breathRange, def.breathHalfAngle, 0.0f, def.breathDuration, round.element,
                             m_nextBreathId++, shooter});
        events.push_back(BreathFired{origin, along, def.breathRange, def.breathHalfAngle, def.breathDuration, round.element, shooter});
        return;
    }
    if (def.self != SelfEffect::None) {
        ElementId element = round.element;
        if (const auto changed = m_volumes.reactAlong(origin, origin + glm::normalize(forward) * 0.5f, element, m_ammo.reactions)) {
            if (m_ammo.elements[*changed].self != SelfEffect::None) {
                element = *changed;
            }
        }
        const ElementDef& self = m_ammo.elements[element];
        const glm::vec3 along = glm::normalize(forward);
        const PlayerBody* body = bodyOf(shooter);
        const glm::vec3 feet = body ? body->feet : origin;
        if (!castHere) {
            castSelfWorld(shooter, element, origin, along, events);
            return;
        }
        if (self.self == SelfEffect::Hit) {
            events.push_back(PlayerHit{element, along});
            hurtPlayer(shooter, 0.35f, origin, events);
            const glm::vec3 up = body ? body->up : upAt(origin);
            pushPlayer(shooter, -glm::normalize(along - up * glm::dot(along, up) + up * 1e-4f) * self.selfHitStagger);
            return;
        }
        glm::vec3 arrive = feet;
        if (self.self == SelfEffect::Reveal) {
            m_reveals.push_back({feet, 0.0f, self.selfSpeed, self.selfDistance, self.selfDuration, {}});
            m_reveals.back().owner = shooter;
        } else if (m_hooks.selfCast) {
            arrive = m_hooks.selfCast(shooter, self, along);
        }
        events.push_back(SelfCast{self.self, element, feet, arrive, self.selfDuration, shooter});
        if (self.self == SelfEffect::Blink && m_host) {
            const glm::vec3 chest = (body ? body->up : upAt(feet)) * 1.1f;
            electrifyFog(feet + chest, arrive + chest, events);
        }
        return;
    }
    if (!m_host) {
        return;
    }
    if (def.hitscanRange > 0.0f) {
        fireHitscan(origin, direction, round, shooter, events);
        return;
    }
    ShotProfile profile;
    profile.velocityScale = def.velocityScale;
    profile.dragScale = def.dragScale;
    profile.ethereal = def.ethereal;
    profile.dragPerMetre = def.ethereal ? def.etherealDrag : 0.0f;
    profile.gravityScale = def.gravityScale;
    profile.fadeSpeed = def.fadeSpeed;
    profile.owner = shooter;
    profile.inheritVelocity = inherit;
    m_ballistics.fire(origin, direction, round, profile);
}

glm::vec3 Gameplay::gravityAt(const glm::vec3& at) const {
    return m_hooks.gravity ? m_hooks.gravity(at) : glm::vec3(0.0f, -9.81f, 0.0f);
}

glm::vec3 Gameplay::upAt(const glm::vec3& at) const {
    const glm::vec3 g = gravityAt(at);
    const float length = glm::length(g);
    return length > 1e-4f ? -g / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

void Gameplay::castSelfWorld(PlayerId shooter, ElementId element, const glm::vec3& muzzle, const glm::vec3& direction,
                             EventList& events) {
    const ElementDef& def = m_ammo.elements[element];
    const PlayerBody* body = bodyOf(shooter);
    const glm::vec3 feet = body ? body->feet : muzzle;
    glm::vec3 arrive = feet;
    if (def.self == SelfEffect::Hit) {
        hurtPlayer(shooter, 0.35f, muzzle, events);
        return;
    }
    if (def.self == SelfEffect::Reveal) {
        m_reveals.push_back({feet, 0.0f, def.selfSpeed, def.selfDistance, def.selfDuration, {}});
        m_reveals.back().owner = shooter;
    } else if (def.self == SelfEffect::Blink) {
        const glm::vec3 chest = (body ? body->up : upAt(feet)) * 1.1f;
        float distance = def.selfDistance;
        if (const auto hit = m_physics.raycast(feet + chest, feet + chest + direction * distance)) {
            distance = glm::distance(feet + chest, hit->point);
        }
        arrive = feet + direction * distance;
        electrifyFog(feet + chest, arrive + chest, events);
    }
    events.push_back(SelfCast{def.self, element, feet, arrive, def.selfDuration, shooter});
}

void Gameplay::fireHitscan(const glm::vec3& origin, const glm::vec3& direction, const Round& round, PlayerId shooter,
                           EventList& events) {
    const float range = m_ammo.elements[round.element].hitscanRange;
    const glm::vec3 end = origin + direction * range;
    const auto hit = raycastShot(origin, end, shooter);
    const glm::vec3 target = hit ? hit->point : end;
    glm::vec3 normal = hit ? hit->normal : glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::dot(normal, direction) > 0.0f) {
        normal = -normal;
    }

    ElementId element = round.element;
    if (const auto changed = m_volumes.reactAlong(origin, target, element, m_ammo.reactions)) {
        element = *changed;
    }
    const ElementDef& def = m_ammo.elements[element];

    if (def.strikeDelay > 0.0f) {
        if (!hit) {
            return;
        }
        const PendingStrike strike{m_nextStrikeId++, target, normal, def.strikeDelay, element};
        m_pendingStrikes.push_back(strike);
        events.push_back(StrikeCalled{strike.id, target, target + upAt(target) * def.strikeCloudHeight, def.strikeDelay});
        return;
    }

    const std::size_t boltEvent = events.size();
    events.push_back(LightningBolt{origin, target, element, 0.0f, shooter});
    electrifyFog(origin, target, events);
    if (hit) {
        ProjectileImpact impact;
        impact.point = target;
        impact.normal = normal;
        impact.direction = direction;
        impact.surface = static_cast<Surface>(hit->userData);
        impact.speed = m_ballistics.tuning().muzzleVelocity;
        impact.element = element;
        impact.body = hit->body;
        impact.dynamicBody = hit->dynamic;
        impact.shooter = shooter;
        resolveImpact(impact, events);
        if (impact.surface == Surface::Ghost) {
            if (const Ghost* struck = m_ghosts.find(impact.body)) {
                std::get<LightningBolt>(events[boltEvent]).hold = struck->hitstop;
            }
        }
    }
}

void Gameplay::resolveImpact(ProjectileImpact impact, EventList& events) {
    if (impact.dynamicBody) {
        impact.bodyPoint = impact.point;
        impact.bodyNormal = impact.normal;
        glm::vec3 position;
        glm::quat rotation;
        m_physics.pose(impact.body, position, rotation);
        const glm::quat inverse = glm::inverse(rotation);
        impact.bodyPoint = inverse * (impact.point - position);
        impact.bodyNormal = inverse * impact.normal;
    }
    applyImpact(impact, events);
    events.push_back(impact);
}

void Gameplay::tickStrikes(float dt, EventList& events) {
    for (PendingStrike& strike : m_pendingStrikes) {
        strike.timeLeft -= dt;
        if (strike.timeLeft > 0.0f) {
            continue;
        }
        const ElementDef& def = m_ammo.elements[strike.element];
        const glm::vec3 up = upAt(strike.point);
        const glm::vec3 cloud = strike.point + up * def.strikeCloudHeight;

        ProjectileImpact impact;
        impact.point = strike.point;
        impact.normal = strike.normal;
        impact.direction = -up;
        impact.speed = m_ballistics.tuning().muzzleVelocity * def.strikePower;
        impact.element = strike.element;
        if (const auto hit = m_physics.raycast(cloud, strike.point - up * 0.3f)) {
            impact.point = hit->point;
            impact.normal = hit->normal;
            impact.surface = static_cast<Surface>(hit->userData);
            impact.body = hit->body;
            impact.dynamicBody = hit->dynamic;
        }
        resolveImpact(impact, events);

        const glm::vec3 center = impact.point + impact.normal * 0.2f;
        applyGust(center, def.strikeRadius, def.strikeImpulse, 1.0f, events);
        m_ghosts.damageArea(center, def.strikeRadius, def.strikePower, def.damage, events);
        for (const PlayerBody& player : std::vector<PlayerBody>(m_bodies)) {
            const glm::vec3 body = bodyPoint(player, player.height * 0.5f);
            const glm::vec3 away = body - center;
            const float distance = glm::length(away);
            if (distance < def.strikeRadius && distance > 1e-3f) {
                const float falloff = 1.0f - distance / def.strikeRadius;
                hurtPlayer(player.id, m_rules.strikeDamage * falloff, center, events);
                pushPlayer(player.id, glm::normalize(away / distance + player.up * 0.6f) * (def.strikeImpulse * 0.6f * falloff));
            }
        }
        m_volumes.spawn(strike.element, center, impact.normal, def.strikeRadius * 0.5f, 0.3f);
        events.push_back(LightningStrike{strike.id, cloud, impact.point, impact.normal, def.strikeRadius});
        electrifyFog(cloud, impact.point, events);
    }
    std::erase_if(m_pendingStrikes, [](const PendingStrike& s) { return s.timeLeft <= 0.0f; });
}

void Gameplay::tickBeforePhysics(float dt, EventList& events) {
    m_simTime += dt;
    std::vector<std::pair<std::uint32_t, glm::vec3>> shotStarts;
    for (const Projectile& p : m_ballistics.projectiles()) {
        shotStarts.emplace_back(p.id, p.position);
    }
    const std::size_t firstBallisticsEvent = events.size();
    m_ballistics.tick(
        dt, [this](const glm::vec3& from, const glm::vec3& to) { return raycastAll(from, to); }, events,
        [this](const glm::vec3& from, const glm::vec3& to, ElementId element, glm::vec3* where) {
            return m_volumes.reactAlong(from, to, element, m_ammo.reactions, where);
        },
        [this](const glm::vec3& from, const glm::vec3& to) { return raycastGhosts(from, to); },
        [this](const Projectile& p, const glm::vec3& from, const glm::vec3& to) { return raycastBodies(from, to, p.owner); });
    for (std::size_t i = firstBallisticsEvent; i < events.size(); ++i) {
        if (const auto* born = std::get_if<ProjectileTransformed>(&events[i]);
            born && m_ammo.elements[born->element].driftSpeed > 0.0f) {
            m_windBorn.push_back(born->projectile);
        }
    }
    for (std::size_t i = firstBallisticsEvent; i < events.size(); ++i) {
        if (auto* impact = std::get_if<ProjectileImpact>(&events[i])) {
            if (impact->dynamicBody) {
                glm::vec3 position;
                glm::quat rotation;
                m_physics.pose(impact->body, position, rotation);
                const glm::quat inverse = glm::inverse(rotation);
                impact->bodyPoint = inverse * (impact->point - position);
                impact->bodyNormal = inverse * impact->normal;
            }
            const ProjectileImpact copy = *impact;
            applyImpact(copy, events);
        }
    }
    for (std::size_t i = firstBallisticsEvent; i < events.size(); ++i) {
        if (const auto* changed = std::get_if<ProjectileTransformed>(&events[i]);
            changed && m_ammo.elements[changed->element].explosionRadiusScale > 0.0f) {
            const ProjectileTransformed copy = *changed;
            m_ballistics.remove(copy.projectile);
            steamExplosion(copy.point, copy.element, events);
        }
    }
    for (const auto& [id, start] : shotStarts) {
        for (const Projectile& p : m_ballistics.projectiles()) {
            const ElementDef& wind = m_ammo.elements[p.round.element];
            if (p.id != id || wind.gustImpulse <= 0.0f || wind.flameCone > 0.0f) {
                continue;
            }
            const glm::vec3 step = p.position - start;
            for (const ElementVolume& v : std::vector<ElementVolume>(m_volumes.all())) {
                if (m_ammo.elements[v.element].driftSpeed > 0.0f &&
                    segmentSphere(start, p.position, v.center + v.normal * 1.5f, v.radius + 1.0f)) {
                    blowVolumes(v.center, 0.1f, step);
                }
            }
        }
    }
    std::erase_if(m_windBorn, [this](std::uint32_t id) {
        return std::none_of(m_ballistics.projectiles().begin(), m_ballistics.projectiles().end(),
                            [id](const Projectile& p) { return p.id == id; });
    });
    punchFog(shotStarts, firstBallisticsEvent, events);
    tickStrikes(dt, events);
    for (const ElementVolume& v : std::vector<ElementVolume>(m_volumes.all())) {
        const float speed = glm::length(v.velocity);
        const glm::vec3 lift = upAt(v.center) * 0.6f;
        if (speed > 1e-3f && m_physics.raycast(v.center + lift, v.center + lift + v.velocity / speed * (v.radius * 0.5f + 0.5f))) {
            m_volumes.push(v.id, glm::vec3(0.0f));
        }
    }
    const std::size_t firstVolumeEvent = events.size();
    m_volumes.tick(dt, m_ammo, events);
    m_volumes.settle(
        dt, [this](ElementId element) { return m_ammo.elements[element].vortex.radius > 0.0f; },
        [this](const glm::vec3& at, const glm::vec3& up) -> std::optional<float> {
            const glm::vec3 from = at + up * ElementVolumes::kStepUp;
            if (const auto hit = raycastWorld(from, at - up * 30.0f)) {
                return glm::dot(hit->point, up);
            }
            return std::nullopt;
        },
        [this](const glm::vec3& at) { return upAt(at); });
    for (std::size_t i = firstVolumeEvent; i < events.size(); ++i) {
        if (const auto* reacted = std::get_if<VolumeReacted>(&events[i]);
            reacted && m_ammo.elements[reacted->element].explosionRadiusScale > 0.0f) {
            const VolumeReacted copy = *reacted;
            steamExplosion(copy.point, copy.element, events);
        } else if (const auto* fanned = std::get_if<VolumeReacted>(&events[i]);
                   fanned && m_ammo.elements[fanned->element].driftSpeed > 0.0f && m_simTime - m_lastGustTime < 0.5) {
            blowVolumes(fanned->point, 0.5f, m_lastGustDirection);
        }
    }
    applyVortices(dt);
    tickFog(dt);
    for (Breath& breath : m_breaths) {
        breath.age += dt;
    }
    std::erase_if(m_breaths, [](const Breath& b) { return b.age >= b.duration; });
}

void Gameplay::tickAfterPhysics(float dt, std::size_t firstEvent, EventList& events) {
    tickDroppedRounds(dt);
    tickMaterialDrops(dt);
    tickGhosts(dt, firstEvent, events);

    std::vector<PlayerId> downNow;
    for (const RosterEntry& entry : m_roster.entries()) {
        if (!entry.downed) {
            continue;
        }
        downNow.push_back(entry.id);
        if (std::find(m_knownDown.begin(), m_knownDown.end(), entry.id) != m_knownDown.end() || entry.zombie) {
            continue;
        }
        if (const NecromiteParams* worm = necromiteRules(); worm && nextRandom() < worm->spawnChance) {
            m_wormsDue.emplace_back(entry.id, worm->spawnDelay);
        }
    }
    m_knownDown = downNow;
    tickNecromiteSpawns(dt);
}

void Gameplay::tickClient(float dt, EventList& events) {
    m_simTime += dt;
    m_ballistics.tick(
        dt, [this](const glm::vec3& from, const glm::vec3& to) { return raycastAll(from, to); }, events,
        [this](const glm::vec3& from, const glm::vec3& to, ElementId element, glm::vec3* where) {
            return m_volumes.reactAlong(from, to, element, m_ammo.reactions, where);
        },
        [this](const glm::vec3& from, const glm::vec3& to) { return raycastGhosts(from, to); },
        [this](const Projectile& p, const glm::vec3& from, const glm::vec3& to) { return raycastBodies(from, to, p.owner); });
    m_ghosts.extrapolate(dt);
    m_volumes.extrapolate(dt);
    applyVortices(dt);
    tickFog(dt);
    tickReveal(m_reveals, m_revealMarks, m_ghosts, dt, events);
}

void Gameplay::applyVortices(float dt) {
    m_vortices.clear();
    for (const ElementVolume& volume : m_volumes.all()) {
        const VortexParams& params = m_ammo.elements[volume.element].vortex;
        if (params.radius <= 0.0f) {
            continue;
        }
        m_vortices.push_back({volume.center - volume.normal * 0.08f, vortexStrength(params, volume.age, volume.lifetime), params, volume.normal});
    }
    if (m_vortices.empty() || !m_host) {
        return;
    }
    const float coupling = m_vortices.front().params.objectCoupling;
    for (const LooseBody& loose : m_loose) {
        if (!m_physics.valid(loose.body)) {
            continue;
        }
        glm::vec3 position;
        glm::quat rotation;
        m_physics.pose(loose.body, position, rotation);
        const glm::vec3 air = airVelocity(m_vortices, position);
        if (glm::dot(air, air) < 1e-4f) {
            continue;
        }
        const float grip = coupling / (0.3f + 0.25f * loose.mass);
        const glm::vec3 deltaV = (air - m_physics.linearVelocity(loose.body)) * std::min(grip * dt, 1.0f);
        m_physics.addImpulse(loose.body, deltaV * loose.mass, position + glm::vec3(0.0f, loose.halfExtents.y * 0.3f, 0.0f));
    }
}

float Gameplay::playerWindGrip() const {
    float grip = 0.0f;
    for (const WindVortex& v : m_vortices) {
        grip = std::max(grip, v.params.playerCoupling);
    }
    return grip;
}

void Gameplay::applyImpact(const ProjectileImpact& impact, EventList& events) {
    if (impact.surface == Surface::Ghost) {
        m_ghosts.damage(impact.body, 1.0f, m_ammo.elements[impact.element].damage, impact.point, events, 0, impact.shooter);
        if (const Ghost* struck = m_ghosts.find(impact.body); struck && impact.projectile != 0) {
            m_ballistics.hold(impact.projectile, struck->hitstop);
        }
    }
    if (impact.surface == Surface::Player) {
        const auto victim = static_cast<PlayerId>(impact.body & 0xFFu);
        const bool head = (impact.body & kHeadHit) != 0;
        const bool ghostly = m_ammo.elements[impact.element].ethereal;
        const float scale = ghostly ? m_rules.etherealScale : 1.0f;
        hurtPlayer(victim, m_rules.roundDamage(m_ammo.elements[impact.element].damage, head) * scale, impact.point - impact.direction,
                   events, false, impact.shooter);
        if (!ghostly) {
            const PlayerBody* struck = bodyOf(victim);
            const glm::vec3 up = struck ? struck->up : upAt(impact.point);
            pushPlayer(victim, (impact.direction - up * glm::dot(impact.direction, up)) * m_rules.hitKnockback);
        }
    }
    if (impact.dynamicBody && m_physics.valid(impact.body)) {
        m_physics.addImpulse(impact.body, impact.direction * (0.01f * impact.speed), impact.point);
    }
    if (impact.ricochet) {
        return;
    }

    const ElementDef& def = m_ammo.elements[impact.element];
    if (def.volumeRadius > 0.0f && def.volumeLifetime > 0.0f) {
        const bool gustOnly = def.gustImpulse > 0.0f && def.flameCone <= 0.0f;
        const bool settles = def.vortex.radius > 0.0f;
        const std::uint32_t volume =
            m_volumes
                .spawn(impact.element, impact.point + impact.normal * (gustOnly || settles ? 0.3f : 0.08f), impact.normal,
                       def.volumeRadius, def.volumeLifetime)
                .id;
        if (def.driftSpeed > 0.0f && std::find(m_windBorn.begin(), m_windBorn.end(), impact.projectile) != m_windBorn.end()) {
            const glm::vec3 up = upAt(impact.point);
            const glm::vec3 along = impact.direction - up * glm::dot(impact.direction, up);
            if (glm::length(along) > 1e-3f) {
                m_volumes.push(volume, glm::normalize(along) * def.driftSpeed);
            }
        }
    }
    if (def.gustImpulse > 0.0f && def.flameCone <= 0.0f) {
        blowVolumes(impact.point, 1.0f, impact.direction);
        m_lastGustDirection = impact.direction;
        m_lastGustTime = m_simTime;
    }
    if (def.blastRadius > 0.0f) {
        const glm::vec3 center = impact.point + impact.normal * 0.1f;
        for (const PlayerBody& body : std::vector<PlayerBody>(m_bodies)) {
            if (body.downed || body.possessed) {
                continue;
            }
            const glm::vec3 shove = blastShove(center, body, def.blastRadius, def.blastKnockback);
            if (glm::dot(shove, shove) > 0.0f) {
                pushPlayer(body.id, shove);
            }
        }
        events.push_back(PressureBurst{center, def.blastRadius});
    }
    if (def.gustImpulse > 0.0f) {
        const bool pureGust = def.flameCone <= 0.0f;
        applyGust(impact.point + impact.normal * 0.2f, std::max(def.volumeRadius, 2.0f), def.gustImpulse,
                  m_fogParams && pureGust ? m_fogParams->gustHole : 0.0f, events);
    }
    if (def.flameCone > 0.0f) {
        glm::vec3 along = impact.direction - impact.normal * glm::dot(impact.direction, impact.normal);
        along = glm::length(along) > 0.2f ? glm::normalize(along) : impact.normal;
        const glm::vec3 coneDir = glm::normalize(along + impact.normal * 0.35f);
        events.push_back(FlameCone{impact.point + impact.normal * 0.1f, coneDir, def.flameCone});

        const ElementId fire = m_ammo.element("fire");
        const ElementDef& fireDef = m_ammo.elements[fire];
        for (int k = 1; k <= 3; ++k) {
            const glm::vec3 at = impact.point + along * (def.flameCone * static_cast<float>(k) / 3.0f);
            m_volumes.spawn(fire, at + impact.normal * 0.08f, impact.normal, fireDef.volumeRadius * 1.2f, fireDef.volumeLifetime);
        }
    }
}

std::optional<engine::RayHit> Gameplay::raycastWorld(const glm::vec3& from, const glm::vec3& to) const {
    return m_physics.raycast(from, to);
}

std::optional<engine::RayHit> Gameplay::raycastGhosts(const glm::vec3& from, const glm::vec3& to) const {
    const auto ghost = m_ghosts.raycast(from, to);
    if (!ghost) {
        return std::nullopt;
    }
    engine::RayHit hit;
    hit.point = ghost->point;
    hit.normal = ghost->normal;
    hit.fraction = ghost->fraction;
    hit.userData = static_cast<std::uint64_t>(Surface::Ghost);
    hit.body = ghost->id;
    return hit;
}

std::optional<engine::RayHit> Gameplay::raycastAll(const glm::vec3& from, const glm::vec3& to) const {
    const auto world = m_physics.raycast(from, to);
    const auto ghost = raycastGhosts(from, to);
    if (ghost && (!world || ghost->fraction < world->fraction)) {
        return ghost;
    }
    return world;
}

std::optional<engine::RayHit> Gameplay::raycastBodies(const glm::vec3& from, const glm::vec3& to, PlayerId shooter) const {
    std::vector<PlayerBody> fair;
    std::span<const PlayerBody> bodies = m_bodies;
    if (!m_rules.friendlyFire) {
        const bool fromZombie = m_roster.zombie(shooter);
        for (const PlayerBody& body : m_bodies) {
            if (body.zombie != fromZombie) {
                fair.push_back(body);
            }
        }
        if (fair.empty()) {
            return std::nullopt;
        }
        bodies = fair;
    }
    const auto hit = raycastPlayers(bodies, from, to, shooter);
    if (!hit) {
        return std::nullopt;
    }
    engine::RayHit out;
    out.point = hit->point;
    out.normal = hit->normal;
    out.fraction = hit->fraction;
    out.userData = static_cast<std::uint64_t>(Surface::Player);
    out.body = hit->id | (hit->head ? kHeadHit : 0u);
    return out;
}

std::optional<engine::RayHit> Gameplay::raycastShot(const glm::vec3& from, const glm::vec3& to, PlayerId shooter) const {
    const auto world = raycastAll(from, to);
    const auto body = raycastBodies(from, to, shooter);
    if (body && (!world || body->fraction < world->fraction)) {
        return body;
    }
    return world;
}

const LooseBody* Gameplay::looseOf(std::uint32_t body) const {
    const auto it = std::find_if(m_loose.begin(), m_loose.end(), [body](const LooseBody& l) { return l.body == body; });
    return it == m_loose.end() ? nullptr : &*it;
}

void Gameplay::tickGhosts(float dt, std::size_t firstEvent, EventList& events) {
    for (const ElementVolume& v : m_volumes.all()) {
        if (m_ammo.elements[v.element].damage != DamageKind::Fire) {
            continue;
        }
        const glm::vec3 heart = v.center + v.normal * (v.radius * 0.5f);
        m_ghosts.damageArea(heart, v.radius, kBurnRoundsPerSecond * dt, DamageKind::Fire, events, kVolumeSource | v.id);
        const VortexParams& funnel = m_ammo.elements[v.element].vortex;
        if (funnel.radius <= 0.0f) {
            continue;
        }
        std::vector<std::uint32_t> lifted;
        for (const Ghost& ghost : m_ghosts.ghosts()) {
            const GhostHitSphere sphere = ghostHitSphere(ghost, m_ghosts.def(ghost));
            const glm::vec3 offset = sphere.center - v.center;
            const float rise = glm::dot(offset, v.normal);
            const float across = glm::length(offset - v.normal * rise);
            const bool inSphere = glm::distance(sphere.center, heart) < v.radius + sphere.radius;
            if (!inSphere && !ghostUntouchable(ghost) && rise > 0.0f && rise < funnel.height &&
                across < funnel.coreRadius * kBurnColumnScale + sphere.radius) {
                lifted.push_back(ghost.id);
            }
        }
        for (const std::uint32_t id : lifted) {
            if (const Ghost* ghost = m_ghosts.find(id)) {
                m_ghosts.damage(id, kBurnRoundsPerSecond * dt, DamageKind::Fire, ghost->position, events, kVolumeSource | v.id);
            }
        }
    }
    for (const Breath& breath : m_breaths) {
        std::vector<std::uint32_t> touched;
        for (const Ghost& ghost : m_ghosts.ghosts()) {
            if (breathTouches(breath, ghost.position)) {
                touched.push_back(ghost.id);
            }
        }
        for (const std::uint32_t id : touched) {
            if (const Ghost* ghost = m_ghosts.find(id)) {
                m_ghosts.damage(id, kBreathRoundsPerSecond * dt, m_ammo.elements[breath.element].damage, ghost->position, events,
                                kBreathSource | breath.id);
            }
        }
    }
    for (const Breath& breath : m_breaths) {
        for (const PlayerBody& player : std::vector<PlayerBody>(m_bodies)) {
            const bool fair = m_rules.friendlyFire || player.zombie != m_roster.zombie(breath.shooter);
            if (player.id == breath.shooter || player.downed || player.possessed || !fair) {
                continue;
            }
            if (breathTouches(breath, bodyPoint(player, player.height * 0.65f))) {
                hurtPlayer(player.id, m_rules.ghostfirePerSecond * dt, breath.origin, events, true, breath.shooter);
            }
        }
    }
    for (const ElementVolume& v : std::vector<ElementVolume>(m_volumes.all())) {
        if (m_ammo.elements[v.element].damage != DamageKind::Fire) {
            continue;
        }
        for (const PlayerBody& player : std::vector<PlayerBody>(m_bodies)) {
            const glm::vec3 d = player.feet - v.center;
            const float along = glm::dot(d, player.up);
            if (glm::length(d - player.up * along) < v.radius && along > -player.height && along < 2.0f) {
                hurtPlayer(player.id, m_rules.burnPerSecond * dt, v.center, events, true);
            }
        }
    }

    std::vector<GhostQuarry> quarries;
    std::vector<PlayerId> quarryIds;
    for (const PlayerBody& player : m_bodies) {
        quarries.push_back({player.feet, player.eye, player.viewDirection, player.hidden || player.downed, player.visibility});
        quarries.back().up = player.up;
        quarries.back().id = player.id;
        quarries.back().downed = player.downed;
        quarries.back().possessed = player.possessed;
        quarries.back().hidden = quarries.back().hidden || player.zombie;
        quarries.back().interacting =
            std::find(m_interacting.begin(), m_interacting.end(), player.id) != m_interacting.end();
        quarryIds.push_back(player.id);
    }
    GhostContext context;
    context.players = quarries;
    context.blocked = [this](const glm::vec3& from, const glm::vec3& to) { return m_physics.raycast(from, to).has_value(); };
    context.raycast = [this](const glm::vec3& from, const glm::vec3& to) -> std::optional<GhostSurfaceHit> {
        if (const auto hit = m_physics.raycast(from, to); hit && !hit->dynamic) {
            return GhostSurfaceHit{hit->point, hit->normal};
        }
        return std::nullopt;
    };
    context.fog = &m_fog;
    context.wind = [this](const glm::vec3& at) { return airVelocity(m_vortices, at); };
    context.gravity = [this](const glm::vec3& at) { return gravityAt(at); };
    std::vector<GhostProp> props;
    for (const LooseBody& loose : m_loose) {
        if (!m_physics.valid(loose.body)) {
            continue;
        }
        glm::vec3 position;
        glm::quat rotation;
        m_physics.pose(loose.body, position, rotation);
        props.push_back({loose.body, position, loose.mass});
    }
    context.props = props;
    context.moveProp = [this](std::uint32_t body, const glm::vec3& velocity) {
        if (m_physics.valid(body)) {
            m_physics.setLinearVelocity(body, velocity);
        }
    };
    const std::size_t firstGhostEvent = events.size();
    m_ghosts.tick(dt, context, events);
    for (std::size_t i = firstGhostEvent; i < events.size(); ++i) {
        if (auto* entered = std::get_if<NecromiteEntered>(&events[i])) {
            const NecromiteEntered was = *entered;
            const PlayerId zombie = m_hooks.raiseZombie ? m_hooks.raiseZombie(was.player) : kNoPlayer;
            std::get<NecromiteEntered>(events[i]).zombie = zombie;
        }
    }
    std::vector<PlayerDamaged> ghostHarm;
    for (std::size_t i = firstGhostEvent; i < events.size(); ++i) {
        if (const auto* hurt = std::get_if<PlayerDamaged>(&events[i])) {
            ghostHarm.push_back(*hurt);
        }
    }
    events.erase(std::remove_if(events.begin() + static_cast<std::ptrdiff_t>(firstGhostEvent), events.end(),
                                [](const GameEvent& e) { return std::holds_alternative<PlayerDamaged>(e); }),
                 events.end());
    for (const PlayerDamaged& hurt : ghostHarm) {
        if (hurt.player >= 0 && static_cast<std::size_t>(hurt.player) < quarryIds.size()) {
            const PlayerId who = quarryIds[static_cast<std::size_t>(hurt.player)];
            hurtPlayer(who, hurt.amount, hurt.from, events);
            if (glm::length(hurt.shove) > 1e-3f) {
                pushPlayer(who, hurt.shove);
            }
        }
    }

    for (std::size_t i = firstEvent; i < events.size(); ++i) {
        if (const auto* died = std::get_if<GhostDied>(&events[i]); died && !died->burst) {
            const GhostDied copy = *died;
            const float pick = nextRandom();
            if (const auto drop = chooseDrop(m_ghostData.types[copy.type], copy.disguise, pick, nextRandom())) {
                for (int n = 0; n < drop->count; ++n) {
                    m_materialDrops.push_back({drop->material, copy.position, upAt(copy.position) * 1.5f});
                    events.push_back(MaterialDropped{copy.position, drop->material});
                }
            }
        }
    }

    for (std::size_t i = firstEvent; i < events.size(); ++i) {
        if (const auto* threw = std::get_if<GhostThrew>(&events[i])) {
            float damage = 0.25f;
            if (const Ghost* thrower = m_ghosts.find(threw->id)) {
                damage = m_ghosts.def(*thrower).poltergeist.throwDamage;
            }
            m_thrown.push_back({threw->body, 0.0f, damage});
        }
    }
    for (ThrownProp& thrown : m_thrown) {
        thrown.age += dt;
        const LooseBody* loose = looseOf(thrown.body);
        if (!loose || !m_physics.valid(loose->body)) {
            continue;
        }
        glm::vec3 position;
        glm::quat rotation;
        m_physics.pose(loose->body, position, rotation);
        const glm::vec3 velocity = m_physics.linearVelocity(loose->body);
        const float speed = glm::length(velocity);
        for (const PlayerBody& player : std::vector<PlayerBody>(m_bodies)) {
            const glm::vec3 nearest = bodyPoint(player, std::clamp(glm::dot(position - player.feet, player.up), 0.2f, player.height));
            const float reach = player.radius + glm::length(loose->halfExtents);
            if (speed > 4.0f && thrown.age < 10.0f && !player.downed && glm::distance(position, nearest) < reach) {
                hurtPlayer(player.id, thrown.damage * std::clamp(0.5f + loose->mass / 12.0f, 0.4f, 1.6f), position, events);
                pushPlayer(player.id, glm::normalize(velocity) * 2.5f);
                thrown.age = 10.0f;
            }
        }
    }
    std::erase_if(m_thrown, [](const ThrownProp& t) { return t.age > 2.5f; });

    std::vector<RevealTarget> seen;
    if (m_rules.arena) {
        for (const PlayerBody& body : m_bodies) {
            if (!body.downed) {
                seen.push_back({body.id, body.feet});
            }
        }
    }
    tickReveal(m_reveals, m_revealMarks, m_ghosts, dt, events, seen);
}

void Gameplay::tickMaterialDrops(float dt) {
    for (MaterialDrop& drop : m_materialDrops) {
        drop.age += dt;
        if (drop.resting) {
            continue;
        }
        const glm::vec3 up = upAt(drop.position);
        drop.velocity -= up * (4.0f * dt);
        const glm::vec3 next = drop.position + drop.velocity * dt;
        if (const auto hit = m_physics.raycast(drop.position, next - up * kMaterialRest)) {
            drop.position = hit->point + up * kMaterialRest;
            drop.resting = true;
        } else {
            drop.position = next;
        }
    }
}

void Gameplay::blowVolumes(const glm::vec3& point, float reach, const glm::vec3& direction) {
    const glm::vec3 up = upAt(point);
    glm::vec3 along = direction - up * glm::dot(direction, up);
    if (glm::length(along) < 1e-3f) {
        return;
    }
    along = glm::normalize(along);
    std::vector<std::uint32_t> blown;
    for (const ElementVolume& v : m_volumes.all()) {
        const float speed = m_ammo.elements[v.element].driftSpeed;
        if (speed > 0.0f && glm::distance(v.center, point) < v.radius + reach) {
            blown.push_back(v.id);
        }
    }
    for (const std::uint32_t id : blown) {
        for (const ElementVolume& v : m_volumes.all()) {
            if (v.id == id) {
                m_volumes.push(id, along * m_ammo.elements[v.element].driftSpeed);
                m_volumes.refresh(id, m_ammo.elements[v.element].vortex.rampIn);
                break;
            }
        }
    }
}

bool Gameplay::steamExplosion(const glm::vec3& point, ElementId steam, EventList& events) {
    const FogCloud* nearest = nullptr;
    float nearestDistance = 1e9f;
    for (const FogCloud& c : m_fog.clouds()) {
        const float distance = glm::distance(c.center, point);
        if (distance < c.radius + 2.0f && distance < nearestDistance) {
            nearest = &c;
            nearestDistance = distance;
        }
    }
    if (!nearest) {
        return false;
    }
    const FogCloud cloud = *nearest;
    const ElementDef& def = m_ammo.elements[steam];
    const float strength = fogCloudDensity(cloud, cloud.center + glm::vec3(0.0f, cloud.height * 0.25f, 0.0f));
    m_volumes.remove(cloud.id);
    m_volumes.removeElement(steam);
    if (strength <= 0.05f) {
        return true;
    }
    const float radius = cloud.radius * def.explosionRadiusScale * (0.5f + 0.5f * strength);
    const glm::vec3 center = glm::mix(point, cloud.center + glm::vec3(0.0f, cloud.height * 0.3f, 0.0f), 0.6f);

    applyGust(center, radius, def.explosionImpulse * strength, 0.0f, events);
    std::vector<std::pair<std::uint32_t, float>> scalded;
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        const float distance = glm::distance(ghost.position, center);
        if (distance < radius) {
            scalded.emplace_back(ghost.id, def.explosionGhostDamage * strength * (1.0f - 0.6f * distance / radius));
        }
    }
    for (const auto& [id, rounds] : scalded) {
        if (const Ghost* ghost = m_ghosts.find(id)) {
            m_ghosts.damage(id, rounds, DamageKind::Fire, ghost->position, events);
        }
    }
    for (const PlayerBody& player : std::vector<PlayerBody>(m_bodies)) {
        const glm::vec3 body = bodyPoint(player, player.height * 0.5f);
        const glm::vec3 away = body - center;
        const float distance = glm::length(away);
        if (distance < radius) {
            const float falloff = 1.0f - distance / radius;
            hurtPlayer(player.id, def.explosionPlayerDamage * strength * falloff, center, events);
            if (distance > 1e-3f) {
                pushPlayer(player.id, glm::normalize(away / distance + player.up * 0.6f) *
                                          (def.explosionImpulse * 0.6f * strength * falloff));
            }
        }
    }
    events.push_back(SteamExplosion{center, cloud.center, cloud.radius, cloud.height, radius, strength, def.explosionLifetime});
    return true;
}

void Gameplay::punchFog(const std::vector<std::pair<std::uint32_t, glm::vec3>>& starts, std::size_t firstEvent,
                        const EventList& events) {
    if (!m_fogParams || m_fog.clouds().empty()) {
        return;
    }
    auto tunnel = [&](const glm::vec3& from, const glm::vec3& to, ElementId element) {
        const ElementDef& def = m_ammo.elements[element];
        const bool wind = def.gustImpulse > 0.0f && def.flameCone <= 0.0f;
        m_fog.punch(from, to, wind ? m_fogParams->windTunnel : m_fogParams->bulletHole,
                    wind ? m_fogParams->blastHoleLife : m_fogParams->holeLife);
    };
    for (const auto& [id, start] : starts) {
        glm::vec3 from = start;
        for (std::size_t i = firstEvent; i < events.size(); ++i) {
            if (const auto* impact = std::get_if<ProjectileImpact>(&events[i]); impact && impact->projectile == id) {
                tunnel(from, impact->point, impact->element);
                from = impact->point;
            }
        }
        for (const Projectile& p : m_ballistics.projectiles()) {
            if (p.id == id && glm::distance(from, p.position) > 0.01f) {
                tunnel(from, p.position, p.round.element);
            }
        }
    }
}

void Gameplay::tickFog(float dt) {
    m_fog.beginSync();
    for (const ElementVolume& v : m_volumes.all()) {
        const FogParams& params = m_ammo.elements[v.element].fog;
        if (params.height <= 0.0f) {
            continue;
        }
        m_fog.syncCloud(v.id, v.center, v.radius, params.height, v.age, v.lifetime);
        if (const FogCloud* cloud = m_fog.cloud(v.id); cloud && (!cloud->measured || glm::distance(cloud->reachCenter, v.center) > 0.25f)) {
            m_fog.setReach(v.id,
                           measureFogReach(v.center, v.radius, params.height,
                                           [this](const glm::vec3& from, const glm::vec3& to) -> std::optional<float> {
                                               if (const auto hit = m_physics.raycast(from, to)) {
                                                   return hit->fraction;
                                               }
                                               return std::nullopt;
                                           }),
                           v.center);
        }
        const WindVortex* nearest = nullptr;
        float nearestDistance = 0.0f;
        for (const WindVortex& w : m_vortices) {
            const float distance = glm::distance(glm::vec2(w.base.x, w.base.z), glm::vec2(v.center.x, v.center.z));
            if (distance < w.params.radius + v.radius && (!nearest || distance < nearestDistance)) {
                nearest = &w;
                nearestDistance = distance;
            }
        }
        if (nearest) {
            glm::vec3 toFunnel = nearest->base - v.center;
            toFunnel.y = 0.0f;
            const glm::vec3 drift = nearestDistance > 0.2f ? toFunnel / nearestDistance * (0.8f * nearest->strength * dt) : glm::vec3(0.0f);
            m_volumes.adjust(v.id, drift, 1.2f * nearest->strength * dt);
            m_fog.pull(v.id, nearest->base, 1.1f * nearest->strength * dt);
        }
    }
    m_fog.endSync();
    m_fog.tick(dt);
}

void Gameplay::electrifyFog(const glm::vec3& from, const glm::vec3& to, EventList& events) {
    if (!m_fogParams) {
        return;
    }
    for (const std::uint32_t id : m_fog.electrify(from, to, m_fogParams->electrifyTime)) {
        const FogCloud* cloud = m_fog.cloud(id);
        if (!cloud) {
            continue;
        }
        events.push_back(FogElectrified{cloud->center, cloud->radius, cloud->height, m_fogParams->electrifyTime});
        std::vector<std::uint32_t> inside;
        for (const Ghost& ghost : m_ghosts.ghosts()) {
            if (fogCloudDensity(*cloud, ghost.position) > 0.15f) {
                inside.push_back(ghost.id);
            }
        }
        const FogCloud charged = *cloud;
        for (const std::uint32_t ghostId : inside) {
            if (const Ghost* ghost = m_ghosts.find(ghostId)) {
                m_ghosts.damage(ghostId, 1.5f, DamageKind::Lightning, ghost->position, events);
            }
        }
        for (const PlayerBody& player : std::vector<PlayerBody>(m_bodies)) {
            if (fogCloudDensity(charged, bodyPoint(player, player.height * 0.6f)) > 0.15f) {
                hurtPlayer(player.id, 0.4f, charged.center, events);
            }
        }
    }
}

void Gameplay::applyGust(const glm::vec3& center, float radius, float deltaV, float fogHoleScale, EventList& events) {
    if (m_fogParams && fogHoleScale > 0.0f) {
        m_fog.blast(center, radius * 0.85f * fogHoleScale, m_fogParams->blastHoleLife);
    }
    for (const LooseBody& loose : m_loose) {
        if (!m_physics.valid(loose.body)) {
            continue;
        }
        glm::vec3 position;
        glm::quat rotation;
        m_physics.pose(loose.body, position, rotation);
        const glm::vec3 d = position - center;
        const float distance = glm::length(d);
        if (distance >= radius || distance < 1e-4f) {
            continue;
        }
        const glm::vec3 up = upAt(position);
        const glm::vec3 dir = glm::normalize(d / distance + up * 0.45f);
        const float strength = deltaV * (1.0f - distance / radius);
        m_physics.addImpulse(loose.body, dir * strength * loose.mass, position + up * 0.05f);
    }
    for (DroppedRound& drop : m_dropped) {
        const glm::vec3 d = drop.position - center;
        const float distance = glm::length(d);
        if (distance < radius && distance > 1e-4f) {
            drop.velocity += glm::normalize(d / distance + upAt(drop.position) * 0.5f) * deltaV * (1.0f - distance / radius) * 0.6f;
            drop.resting = false;
        }
    }
    m_ghosts.push(center, radius, deltaV);
    events.push_back(GustBurst{center, radius, deltaV, m_fogParams ? radius * 0.85f * fogHoleScale : 0.0f});
}

std::vector<net::RoundWire> Gameplay::dropRounds(const ChambersEjected& ejected, const glm::vec3& origin,
                                                 const glm::vec3& forward, const glm::vec3& right) {
    std::vector<net::RoundWire> made;
    for (const Chamber& chamber : ejected.contents) {
        if (chamber.state != ChamberState::Live) {
            continue;
        }
        DroppedRound drop;
        drop.round = chamber.round;
        drop.position = origin + (right * (nextRandom() - 0.5f) + forward * (nextRandom() - 0.5f)) * 0.05f;
        drop.velocity = right * (nextRandom() - 0.5f) * 0.8f + forward * (nextRandom() * 0.4f) - upAt(origin) * 0.4f;
        drop.yaw = nextRandom() * glm::two_pi<float>();
        made.push_back({drop.position, drop.velocity, drop.yaw, drop.round, false});
        if (m_dropped.size() < 64) {
            m_dropped.push_back(drop);
        }
    }
    return made;
}

void Gameplay::adoptRounds(std::span<const net::RoundWire> rounds) {
    for (const net::RoundWire& round : rounds) {
        if (round.round.element < m_ammo.elements.size() && m_dropped.size() < 64) {
            m_dropped.push_back({round.position, round.velocity, round.yaw, round.round, false});
        }
    }
}

void Gameplay::tickDroppedRounds(float dt) {
    for (DroppedRound& drop : m_dropped) {
        const glm::vec3 air = airVelocity(m_vortices, drop.position);
        const bool windy = glm::dot(air, air) > 0.09f;
        if (windy) {
            drop.resting = false;
            drop.velocity += (air - drop.velocity) * std::min(5.0f * dt, 1.0f);
        }
        if (drop.resting) {
            continue;
        }
        drop.velocity += gravityAt(drop.position) * dt;
        const glm::vec3 next = drop.position + drop.velocity * dt;
        if (const auto hit = m_physics.raycast(drop.position, next)) {
            drop.position = hit->point + hit->normal * kRoundRadius;
            if (windy) {
                drop.velocity -= hit->normal * glm::dot(drop.velocity, hit->normal);
            } else {
                drop.velocity = glm::vec3(0.0f);
                drop.resting = true;
            }
        } else {
            drop.position = next;
        }
    }
}

std::optional<Round> Gameplay::tickPickups(PlayerId player, PlayerGun& gun, bool interacting, float dt, EventList& events) {
    const PlayerBody* body = bodyOf(player);
    if (!body || body->downed) {
        gun.pickupProgress = 0.0f;
        return std::nullopt;
    }
    const glm::vec3 at = body->feet;
    int nearestDrop = -1;
    float nearestDropDistance = kPickupRadius * 1.5f;
    for (std::size_t i = 0; i < m_materialDrops.size(); ++i) {
        const MaterialDrop& drop = m_materialDrops[i];
        const glm::vec3 d = drop.position - at;
        const float rise = glm::dot(d, body->up);
        const float distance = glm::length(d - body->up * rise);
        if (drop.resting && distance < nearestDropDistance && std::abs(rise) < 2.5f) {
            nearestDrop = static_cast<int>(i);
            nearestDropDistance = distance;
        }
    }
    int nearestRound = -1;
    float nearestRoundDistance = kPickupRadius;
    for (std::size_t i = 0; i < m_dropped.size(); ++i) {
        const DroppedRound& drop = m_dropped[i];
        const glm::vec3 d = drop.position - at;
        const float rise = glm::dot(d, body->up);
        const float distance = glm::length(d - body->up * rise);
        if (drop.resting && distance < nearestRoundDistance && std::abs(rise) < 1.8f) {
            nearestRound = static_cast<int>(i);
            nearestRoundDistance = distance;
        }
    }
    if (!interacting || (nearestDrop < 0 && nearestRound < 0)) {
        gun.pickupProgress = 0.0f;
        return std::nullopt;
    }
    gun.pickupProgress += dt;
    if (gun.pickupProgress < kPickupTime) {
        return std::nullopt;
    }
    gun.pickupProgress = 0.0f;
    if (!m_host) {
        return std::nullopt;
    }
    if (nearestDrop >= 0) {
        const MaterialDrop taken = m_materialDrops[static_cast<std::size_t>(nearestDrop)];
        m_materials.add(taken.material, 1);
        m_materialDrops.erase(m_materialDrops.begin() + nearestDrop);
        events.push_back(MaterialPickedUp{taken.position, taken.material, player});
        return std::nullopt;
    }
    const DroppedRound taken = m_dropped[static_cast<std::size_t>(nearestRound)];
    gun.pouch.add(taken.round.element);
    m_dropped.erase(m_dropped.begin() + nearestRound);
    events.push_back(RoundPickedUp{});
    return taken.round;
}

const NecromiteParams* Gameplay::necromiteRules() const {
    for (const GhostDef& def : m_ghostData.types) {
        if (def.behavior == GhostBehavior::Necromite) {
            return &def.necromite;
        }
    }
    return nullptr;
}

void Gameplay::tickNecromiteSpawns(float dt) {
    const NecromiteParams* rules = necromiteRules();
    const auto type = m_ghostData.find("necromite");
    if (!rules || !type || m_rules.arena || !m_haunted) {
        m_wormsDue.clear();
        return;
    }
    for (auto& [player, left] : m_wormsDue) {
        left -= dt;
    }
    for (std::size_t i = 0; i < m_wormsDue.size();) {
        const PlayerId player = m_wormsDue[i].first;
        if (m_wormsDue[i].second > 0.0f) {
            ++i;
            continue;
        }
        m_wormsDue.erase(m_wormsDue.begin() + static_cast<std::ptrdiff_t>(i));
        const RosterEntry* entry = m_roster.find(player);
        const PlayerBody* body = bodyOf(player);
        const bool someoneStands = std::any_of(m_roster.entries().begin(), m_roster.entries().end(),
                                               [](const RosterEntry& e) { return !e.downed && !e.zombie; });
        const bool wormAlready = std::any_of(m_ghosts.ghosts().begin(), m_ghosts.ghosts().end(), [&](const Ghost& g) {
            return m_ghosts.def(g).behavior == GhostBehavior::Necromite && g.prefers == player;
        });
        if (!entry || !body || !entry->downed || entry->possessedBy != kNoPlayer || !someoneStands || wormAlready) {
            continue;
        }
        const float angle = nextRandom() * glm::two_pi<float>();
        const glm::vec3 up = body->up;
        glm::vec3 at = body->feet + ringAround(up, angle) * glm::mix(rules->spawnMin, rules->spawnMax, nextRandom());
        if (const auto ground = m_physics.raycast(at + up * 2.0f, at - up * 6.0f)) {
            at = ground->point;
        }
        const std::uint32_t worm = m_ghosts.spawn(*type, at + up * 0.2f, up);
        m_ghosts.setOn(worm, player);
    }
}

std::uint32_t Gameplay::spawnGhost(std::string_view type, const glm::vec3& at) {
    if (const auto def = m_ghostData.find(type)) {
        return m_ghosts.spawn(*def, at, upAt(at));
    }
    return 0;
}

void Gameplay::clearWorld() {
    m_ghosts.replace({});
    m_volumes.replace({});
    m_ballistics = Ballistics{};
    m_ballistics.setFields(m_hooks.gravity, m_hooks.wind);
    m_ballistics.setRicochet(ricochetScales(m_ammo));
    m_pendingStrikes.clear();
    m_breaths.clear();
    m_reveals.clear();
    m_revealMarks.clear();
    m_windBorn.clear();
    m_thrown.clear();
    m_dropped.clear();
    m_materialDrops.clear();
    m_vortices.clear();
    m_fog = FogField{};
    m_wormsDue.clear();
}

void Gameplay::hurtPlayer(PlayerId id, float amount, const glm::vec3& from, EventList& events, bool continuous, PlayerId by) {
    if (!m_host) {
        return;
    }
    m_roster.hurt(id, amount, from, events, continuous, by);
}

void Gameplay::pushPlayer(PlayerId id, const glm::vec3& deltaVelocity) {
    if (m_hooks.push) {
        m_hooks.push(id, deltaVelocity);
    }
}

const PlayerBody* Gameplay::bodyOf(PlayerId id) const {
    const auto it = std::find_if(m_bodies.begin(), m_bodies.end(), [id](const PlayerBody& b) { return b.id == id; });
    return it == m_bodies.end() ? nullptr : &*it;
}

void Gameplay::snapshot(GameplaySnapshot& out) const {
    out.ghosts = m_ghosts.ghosts();
    out.volumes = m_volumes.all();
    out.materialDrops.clear();
    for (const MaterialDrop& drop : m_materialDrops) {
        out.materialDrops.push_back({drop.material, drop.position, drop.velocity, drop.resting, drop.age});
    }
    out.rounds.clear();
    for (const DroppedRound& drop : m_dropped) {
        out.rounds.push_back({drop.position, drop.velocity, drop.yaw, drop.round, drop.resting});
    }
    out.materials.clear();
    for (std::size_t m = 0; m < m_materials.kinds(); ++m) {
        out.materials.push_back(m_materials.count(static_cast<MaterialId>(m)));
    }
}

void Gameplay::applySnapshot(const GameplaySnapshot& in) {
    m_ghosts.replace(in.ghosts);
    m_volumes.replace(in.volumes);
    m_materialDrops.clear();
    for (const net::DropWire& drop : in.materialDrops) {
        m_materialDrops.push_back({static_cast<MaterialId>(drop.material), drop.position, drop.velocity, drop.resting, drop.age});
    }
    m_dropped.clear();
    for (const net::RoundWire& round : in.rounds) {
        m_dropped.push_back({round.position, round.velocity, round.yaw, round.round, round.resting});
    }
    for (std::size_t m = 0; m < in.materials.size(); ++m) {
        m_materials.set(static_cast<MaterialId>(m), in.materials[m]);
    }
}

std::uint64_t Gameplay::checksum() const {
    std::uint64_t h = 1469598103934665603ull;
    for (const Ghost& ghost : m_ghosts.ghosts()) {
        hashBytes(h, &ghost.position, sizeof(ghost.position));
        hashBytes(h, &ghost.health, sizeof(ghost.health));
    }
    for (const ElementVolume& v : m_volumes.all()) {
        hashBytes(h, &v.center, sizeof(v.center));
    }
    for (const Projectile& p : m_ballistics.projectiles()) {
        hashBytes(h, &p.position, sizeof(p.position));
    }
    for (const DroppedRound& d : m_dropped) {
        hashBytes(h, &d.position, sizeof(d.position));
    }
    hashBytes(h, &m_rng, sizeof(m_rng));
    return h;
}

}
