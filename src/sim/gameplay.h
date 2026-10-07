#pragma once

#include "engine/physics/physics_world.h"
#include "game/ammo/ammo_data.h"
#include "game/ammo/pouch.h"
#include "game/ammo/speedloader.h"
#include "game/ballistics/ballistics.h"
#include "game/events.h"
#include "game/ghosts/ghost_world.h"
#include "game/net/protocol.h"
#include "game/player/player_hit.h"
#include "game/player/roster.h"
#include "game/weapons/mechanism_view.h"
#include "game/weapons/revolver_mechanism.h"
#include "game/world/breath.h"
#include "game/world/element_volume.h"
#include "game/world/fog_field.h"
#include "game/world/reveal.h"
#include "game/world/vortex_field.h"

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace ghost::game {

struct GunCommand {
    bool trigger = false;
    bool cock = false;
    bool toggleCylinder = false;
    bool closeCylinder = false;
    bool eject = false;
    bool speedload = false;
    int turn = 0;
    bool load = false;
    ElementId loadElement = kPlainElement;
    bool quickFill = false;
    ElementId quickFillElement = kPlainElement;
    glm::vec3 muzzle{0.0f};
    glm::vec3 barrelDirection{0.0f, 0.0f, -1.0f};
};

struct PlayerGun {
    RevolverMechanism mechanism;
    AmmoPouch pouch = AmmoPouch(0);
    SpeedloaderRack speedloaders;
    std::optional<Round> pendingLoad;
    bool quickFill = false;
    ElementId quickFillElement = kPlainElement;
    bool closeLatched = false;
    int turnLatched = 0;
    float pickupProgress = 0.0f;
    int pickupPulse = 0;
    MechanismView shown;
};

struct LooseBody {
    engine::PhysicsWorld::BodyHandle body = engine::kNoBody;
    glm::vec3 halfExtents{0.2f};
    float mass = 5.0f;
};

struct DroppedRound {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    Round round;
    bool resting = false;
};

struct MaterialDrop {
    MaterialId material = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    bool resting = false;
    float age = 0.0f;
};

struct PendingStrike {
    std::uint32_t id = 0;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float timeLeft = 0.0f;
    ElementId element = kPlainElement;
};

struct GameplayHooks {
    std::function<void(PlayerId, const glm::vec3&)> push;
    std::function<glm::vec3(PlayerId, const ElementDef&, const glm::vec3& direction)> selfCast;
    std::function<PlayerId(PlayerId)> raiseZombie;
    std::function<glm::vec3(const glm::vec3&)> gravity;
    std::function<glm::vec3(const glm::vec3&)> wind;
};

struct GameplaySnapshot {
    std::vector<Ghost> ghosts;
    std::vector<ElementVolume> volumes;
    std::vector<net::DropWire> materialDrops;
    std::vector<net::RoundWire> rounds;
    std::vector<std::int32_t> materials;
};

std::vector<float> ricochetScales(const AmmoData& ammo);

class Gameplay {
public:
    Gameplay(Roster& roster, const PlayerRules& rules, engine::PhysicsWorld& physics);

    void setHooks(GameplayHooks hooks) {
        m_hooks = std::move(hooks);
        m_ballistics.setFields(m_hooks.gravity, m_hooks.wind);
        m_ballistics.setRicochet(ricochetScales(m_ammo));
    }
    void setBodies(std::vector<PlayerBody> bodies) { m_bodies = std::move(bodies); }
    void setLoose(std::vector<LooseBody> loose) { m_loose = std::move(loose); }
    void setInteracting(std::vector<PlayerId> interacting) { m_interacting = std::move(interacting); }
    void setHost(bool host) { m_host = host; }

    void stockGun(PlayerGun& gun) const;
    void arenaKit(PlayerGun& gun) const;
    void tickGun(PlayerId owner, PlayerGun& gun, const GunCommand& command, bool holstered, bool downed, float dt,
                 EventList& events, std::vector<ShotFired>& shots, std::vector<ChambersEjected>& ejected);
    MechanismView viewOf(const PlayerGun& gun, float alpha, float time) const;

    void fireShot(PlayerId shooter, const glm::vec3& origin, const glm::vec3& forward, float spreadDeg, const Round& round,
                  bool castHere, EventList& events, const glm::vec3& inherit = glm::vec3(0.0f));
    std::vector<net::RoundWire> dropRounds(const ChambersEjected& ejected, const glm::vec3& origin, const glm::vec3& forward,
                                           const glm::vec3& right);
    const NecromiteParams* necromiteRules() const;
    void forgetPlayers() { m_ghosts.forget(); m_wormsDue.clear(); }
    void addReveal(const RevealPulse& pulse) { m_reveals.push_back(pulse); }
    void adoptRounds(std::span<const net::RoundWire> rounds);

    void tickBeforePhysics(float dt, EventList& events);
    void tickAfterPhysics(float dt, std::size_t firstEvent, EventList& events);
    void tickClient(float dt, EventList& events);
    std::optional<Round> tickPickups(PlayerId player, PlayerGun& gun, bool interacting, float dt, EventList& events);

    std::uint32_t spawnGhost(std::string_view type, const glm::vec3& at);
    bool poseGhost(std::uint32_t id, const GhostWorld::Pose& pose) { return m_ghosts.pose(id, pose); }
    bool removeGhost(std::uint32_t id) { return m_ghosts.remove(id); }
    void clearWorld();

    void hurtPlayer(PlayerId id, float amount, const glm::vec3& from, EventList& events, bool continuous = false,
                    PlayerId by = kNoPlayer);
    void pushPlayer(PlayerId id, const glm::vec3& deltaVelocity);
    const PlayerBody* bodyOf(PlayerId id) const;
    glm::vec3 gravityAt(const glm::vec3& at) const;
    glm::vec3 upAt(const glm::vec3& at) const;

    void snapshot(GameplaySnapshot& out) const;
    void applySnapshot(const GameplaySnapshot& in);

    const AmmoData& ammo() const { return m_ammo; }
    const GhostData& ghostData() const { return m_ghostData; }
    const GhostWorld& ghosts() const { return m_ghosts; }
    const ElementVolumes& volumes() const { return m_volumes; }
    const std::vector<WindVortex>& vortices() const { return m_vortices; }
    const FogField& fog() const { return m_fog; }
    const FogParams* fogParams() const { return m_fogParams; }
    ElementId fogElement() const { return m_fogElement; }
    const std::vector<Breath>& breaths() const { return m_breaths; }
    const std::vector<RevealPulse>& reveals() const { return m_reveals; }
    const std::vector<RevealMark>& revealMarks() const { return m_revealMarks; }
    const std::vector<DroppedRound>& dropped() const { return m_dropped; }
    const std::vector<MaterialDrop>& materialDrops() const { return m_materialDrops; }
    const Ballistics& ballistics() const { return m_ballistics; }
    Ballistics& ballistics() { return m_ballistics; }
    const std::vector<PendingStrike>& pendingStrikes() const { return m_pendingStrikes; }
    const MaterialInventory& materials() const { return m_materials; }
    MaterialInventory& materials() { return m_materials; }
    const std::vector<PlayerBody>& bodies() const { return m_bodies; }
    glm::vec3 airVelocityAt(const glm::vec3& at) const { return airVelocity(m_vortices, at); }
    float playerWindGrip() const;
    double simTime() const { return m_simTime; }
    float nextRandom();
    std::uint64_t checksum() const;

private:
    void castSelfWorld(PlayerId shooter, ElementId element, const glm::vec3& muzzle, const glm::vec3& direction,
                       EventList& events);
    void fireHitscan(const glm::vec3& origin, const glm::vec3& direction, const Round& round, PlayerId shooter,
                     EventList& events);
    void resolveImpact(ProjectileImpact impact, EventList& events);
    void applyImpact(const ProjectileImpact& impact, EventList& events);
    void tickStrikes(float dt, EventList& events);
    void applyVortices(float dt);
    void tickGhosts(float dt, std::size_t firstEvent, EventList& events);
    void blowVolumes(const glm::vec3& point, float reach, const glm::vec3& direction);
    bool steamExplosion(const glm::vec3& point, ElementId steam, EventList& events);
    void punchFog(const std::vector<std::pair<std::uint32_t, glm::vec3>>& starts, std::size_t firstEvent,
                  const EventList& events);
    void tickFog(float dt);
    void electrifyFog(const glm::vec3& from, const glm::vec3& to, EventList& events);
    void applyGust(const glm::vec3& center, float radius, float deltaV, float fogHoleScale, EventList& events);
    void tickDroppedRounds(float dt);
    void tickMaterialDrops(float dt);
    void tickNecromiteSpawns(float dt);
    std::optional<engine::RayHit> raycastWorld(const glm::vec3& from, const glm::vec3& to) const;
    std::optional<engine::RayHit> raycastGhosts(const glm::vec3& from, const glm::vec3& to) const;
    std::optional<engine::RayHit> raycastAll(const glm::vec3& from, const glm::vec3& to) const;
    std::optional<engine::RayHit> raycastBodies(const glm::vec3& from, const glm::vec3& to, PlayerId shooter) const;
    std::optional<engine::RayHit> raycastShot(const glm::vec3& from, const glm::vec3& to, PlayerId shooter) const;
    const LooseBody* looseOf(std::uint32_t body) const;

    struct ThrownProp {
        std::uint32_t body = 0;
        float age = 0.0f;
        float damage = 0.25f;
    };

    Roster& m_roster;
    const PlayerRules& m_rules;
    engine::PhysicsWorld& m_physics;
    GameplayHooks m_hooks;
    bool m_host = true;

    AmmoData m_ammo;
    GhostData m_ghostData;
    GhostWorld m_ghosts{m_ghostData};
    Ballistics m_ballistics;
    ElementVolumes m_volumes;
    std::vector<WindVortex> m_vortices;
    std::vector<PendingStrike> m_pendingStrikes;
    FogField m_fog;
    std::vector<Breath> m_breaths;
    std::vector<RevealPulse> m_reveals;
    std::vector<RevealMark> m_revealMarks;
    std::vector<std::uint32_t> m_windBorn;
    std::vector<ThrownProp> m_thrown;
    std::vector<DroppedRound> m_dropped;
    std::vector<MaterialDrop> m_materialDrops;
    MaterialInventory m_materials;
    std::vector<PlayerBody> m_bodies;
    std::vector<LooseBody> m_loose;
    std::vector<PlayerId> m_interacting;
    std::vector<std::pair<PlayerId, float>> m_wormsDue;
    std::vector<PlayerId> m_knownDown;
    glm::vec3 m_lastGustDirection{0.0f};
    double m_lastGustTime = -10.0;
    double m_simTime = 0.0;
    const FogParams* m_fogParams = nullptr;
    ElementId m_fogElement = kPlainElement;
    std::uint32_t m_nextStrikeId = 1;
    std::uint32_t m_nextBreathId = 1;
    std::uint32_t m_rng = 0x9E3779B9u;
    bool m_haunted = true;
};

}
