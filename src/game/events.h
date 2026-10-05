#pragma once

#include "game/car_events.h"
#include "game/ammo/round.h"
#include "game/ammo/element.h"
#include "game/ballistics/surface.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <variant>
#include <vector>

namespace ghost::game {
inline constexpr int kChamberCount = 6;

enum class ChamberState : std::uint8_t { Empty, Live, Spent };

struct Chamber {
    ChamberState state = ChamberState::Empty;
    Round round;
};

struct ShotFired {
    Round round;
    int chamber = 0;
    std::uint8_t player = 0;
    bool last = false;
};

struct DryFired {
    int chamber = 0;
};

struct HammerCocked {};

struct ProjectileImpact {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    Surface surface = Surface::Ground;
    float speed = 0.0f;
    bool ricochet = false;
    ElementId element = kPlainElement;
    std::uint32_t projectile = 0;
    std::uint32_t body = 0;
    bool dynamicBody = false;
    bool head = false;
    std::uint8_t shooter = 255;

    glm::vec3 bodyPoint{0.0f};
    glm::vec3 bodyNormal{0.0f, 1.0f, 0.0f};
};

struct ProjectileTransformed {
    std::uint32_t projectile = 0;
    glm::vec3 point{0.0f};
    ElementId element = kPlainElement;
};

struct ProjectileFaded {
    glm::vec3 point{0.0f};
    ElementId element = kPlainElement;
    std::uint32_t projectile = 0;
    std::uint8_t shooter = 255;
};

struct PlayerHit {
    ElementId element = kPlainElement;
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

struct SelfCast {
    SelfEffect effect = SelfEffect::None;
    ElementId element = kPlainElement;
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float duration = 0.0f;
    std::uint8_t player = 0;
};

struct FogElectrified {
    glm::vec3 center{0.0f};
    float radius = 4.0f;
    float height = 3.0f;
    float duration = 1.2f;
};

struct BreathFired {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float range = 5.0f;
    float halfAngle = 0.28f;
    float duration = 0.5f;
    ElementId element = kPlainElement;
    std::uint8_t player = 0;
};

struct GhostHurt {
    std::uint32_t id = 0;
    glm::vec3 point{0.0f};
    float amount = 0.0f;
    DamageKind kind = DamageKind::Plain;
    bool killed = false;
    float hitstop = 0.0f;
};

struct GhostDodged {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f};
};

struct GhostBurst {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    float radius = 1.0f;
};

struct GhostThrew {
    std::uint32_t id = 0;
    std::uint32_t body = 0;
    glm::vec3 from{0.0f};
    glm::vec3 velocity{0.0f};
};

struct GhostDied {
    std::uint32_t id = 0;
    std::uint16_t type = 0;
    glm::vec3 position{0.0f};
    bool burst = false;
    std::int16_t disguise = -1;
};

struct PlayerDamaged {
    float amount = 0.0f;
    glm::vec3 from{0.0f};
    int player = 0;
    glm::vec3 shove{0.0f};
};

struct PlayerDowned {
    std::uint8_t player = 0;
    std::uint8_t by = 255;
};

struct PlayerRespawned {
    std::uint8_t player = 0;
};

struct PlayerRevived {
    std::uint8_t player = 0;
    std::uint8_t by = 0;
};

struct PlayerDied {};

struct MaterialDropped {
    glm::vec3 position{0.0f};
    std::uint16_t material = 0;
};
struct MaterialPickedUp {
    glm::vec3 position{0.0f};
    std::uint16_t material = 0;
    std::uint8_t player = 0;
};

struct GhostRevealed {
    std::uint32_t id = 0;
    float duration = 2.0f;
    glm::vec3 position{0.0f};
    float radius = 0.3f;
};

struct PlayerRevealed {
    std::uint8_t player = 0;
    float duration = 2.0f;
    glm::vec3 position{0.0f};
};

struct SteamExplosion {
    glm::vec3 center{0.0f};
    glm::vec3 cloudCenter{0.0f};
    float cloudRadius = 6.0f;
    float cloudHeight = 4.5f;
    float radius = 6.0f;
    float strength = 1.0f;
    float lifetime = 1.6f;
};

struct VolumeReacted {
    glm::vec3 point{0.0f};
    ElementId element = kPlainElement;
};

struct PressureBurst {
    glm::vec3 center{0.0f};
    float radius = 1.0f;
};

struct GustBurst {
    glm::vec3 center{0.0f};
    float radius = 1.0f;
    float impulse = 1.0f;
    float fogHole = 0.0f;
};

struct FlameCone {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float length = 1.0f;
};

struct LightningBolt {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    ElementId element = kPlainElement;
    float hold = 0.0f;
    std::uint8_t player = 0;
};

struct StrikeCalled {
    std::uint32_t id = 0;
    glm::vec3 point{0.0f};
    glm::vec3 cloud{0.0f};
    float delay = 1.2f;
};

struct LightningStrike {
    std::uint32_t id = 0;
    glm::vec3 cloud{0.0f};
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float radius = 3.5f;
};

struct CylinderOpened {};
struct CylinderClosed {};

struct ChambersEjected {
    std::array<Chamber, kChamberCount> contents{};
};

struct SpeedloaderUsed {};

struct GunHolstered {
    bool away = true;
};

struct RoundLoaded {
    int chamber = 0;
    Round round;
};

struct RoundPickedUp {};

struct BallArcCharged {
    std::uint32_t id = 0;
    glm::vec3 to{0.0f};
    float delay = 0.35f;
};

struct BallArc {
    std::uint32_t id = 0;
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
};

struct BallHopped {
    std::uint32_t id = 0;
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
};

struct MimicRevealed {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    std::int16_t disguise = -1;
};

struct KrakaScattered {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
};
struct KrakaDive {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
};
struct KrakaBall {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
};
struct KrakaBurst {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    float radius = 3.0f;
};
struct KrakaMerged {
    std::uint32_t into = 0;
    std::uint32_t from = 0;
    glm::vec3 position{0.0f};
};
struct KrakaSplit {
    std::uint32_t id = 0;
    std::uint32_t half = 0;
    glm::vec3 position{0.0f};
};

struct NecromiteEmerged {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
};
struct NecromiteEntered {
    std::uint32_t id = 0;
    std::uint8_t player = 0;
    std::uint8_t zombie = 255;
    glm::vec3 position{0.0f};
};
struct ZombieFell {
    std::uint8_t player = 0;
    std::uint8_t zombie = 0;
    glm::vec3 position{0.0f};
};

struct MimicConcealed {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
};

struct MimicThrash {
    std::uint32_t id = 0;
    glm::vec3 position{0.0f};
    glm::vec3 toward{0.0f};
    float windup = 0.3f;
};

using GameEvent = std::variant<ShotFired, DryFired, HammerCocked, ProjectileImpact, ProjectileTransformed,
                               ProjectileFaded, PlayerHit, SelfCast, FogElectrified, BreathFired, GhostHurt,
                               GhostDodged, GhostBurst, GhostThrew, GhostDied, PlayerDamaged, PlayerDied, PlayerDowned, PlayerRespawned, PlayerRevived, MaterialDropped,
                               MaterialPickedUp, GhostRevealed, PlayerRevealed, SteamExplosion, SpeedloaderUsed,
                               VolumeReacted, PressureBurst, GustBurst, FlameCone, LightningBolt, StrikeCalled, LightningStrike,
                               CylinderOpened, CylinderClosed, ChambersEjected,
                               RoundLoaded, RoundPickedUp, GunHolstered, BallArcCharged, BallArc, BallHopped,
                               MimicRevealed, MimicThrash, MimicConcealed, KrakaScattered, KrakaDive, KrakaBall,
                               KrakaBurst, KrakaMerged, KrakaSplit, NecromiteEmerged, NecromiteEntered, ZombieFell,
                               TravelArmed, TravelStarted, TravelArrived, TowerBreached, TapeWritten, PortLinked,
                               CableDropped, TerminalClicked, TerminalPowered, PhotoTaken, DoorMoved, HoodMoved,
                               TrunkMoved, EngineStarted, CarImpact, PartInstalled, PartRemoved, ZoneLoaded,
                               PlayerPlaced, SeatRefused>;
using EventList = std::vector<GameEvent>;

}
