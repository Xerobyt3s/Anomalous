#pragma once

#include "game/events.h"
#include "game/ghosts/ghost_data.h"
#include "game/world/fog_field.h"

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <vector>

namespace ghost::game {
enum class GhostState : std::uint8_t {
    Wander, Lure, Rush, Flinch, Lift, Recover,
    Charge, Hop,
    Disguised, Reveal, Hunt, Pause, Leap, Windup, Flee, Conceal,
    Roost, Circle, Stoop, Gather, Fall, Reform, Scatter, Merge,
    Emerge, Squirm, Bore, Burrow
};
constexpr std::uint32_t kNoProp = 0xFFFFFFFFu;
const char* ghostStateName(GhostState state);

struct GhostHitSource {
    std::uint32_t id = 0;
    float timeLeft = 0.0f;
};

struct Ghost {
    std::uint32_t id = 0;
    GhostTypeId type = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 home{0.0f};
    float health = 1.0f;
    GhostState state = GhostState::Wander;
    float stateTime = 0.0f;
    float age = 0.0f;
    float seed = 0.0f;

    bool perceives = false;
    int target = -1;
    glm::vec3 lastKnown{0.0f};
    float sinceSeen = 1e6f;

    float awayTime = 0.0f;
    float dodging = 0.0f;
    std::uint32_t heldProp = kNoProp;
    glm::vec3 liftFrom{0.0f};
    std::int16_t disguise = -1;
    bool attached = true;
    glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float timer = 0.0f;

    float cooldown = 0.0f;
    glm::vec3 goal{0.0f};
    bool leapHit = false;
    std::uint8_t struck = 0;
    std::int8_t climb = 1;
    std::uint8_t prefers = 255;
    std::uint8_t quarryId = 255;
    std::uint32_t mate = 0;
    bool lastBall = false;

    bool posed = false;
    float poseLoop = 0.0f;
    std::uint8_t hopsLeft = 0;
    std::int8_t hopSide = 1;
    std::array<glm::vec4, 3> arcs{};

    float hitstop = 0.0f;
    bool dying = false;
    float caught = 0.0f;
    float calm = 0.0f;
    glm::vec3 spin{0.0f};
    std::array<GhostHitSource, 4> sources{};
};

struct GhostQuarry {
    glm::vec3 feet{0.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 viewDirection{0.0f, 0.0f, -1.0f};
    bool hidden = false;
    float visibility = 1.0f;
    bool interacting = false;
    std::uint8_t id = 0;
    bool downed = false;
    bool possessed = false;
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

struct GhostProp {
    std::uint32_t body = 0;
    glm::vec3 position{0.0f};
    float mass = 1.0f;
};

struct GhostSurfaceHit {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

struct GhostContext {
    std::span<const GhostQuarry> players;
    std::function<bool(const glm::vec3& from, const glm::vec3& to)> blocked;
    const FogField* fog = nullptr;
    std::function<glm::vec3(const glm::vec3&)> wind;
    std::span<const GhostProp> props;
    std::function<void(std::uint32_t body, const glm::vec3& velocity)> moveProp;

    std::function<std::optional<GhostSurfaceHit>(const glm::vec3& from, const glm::vec3& to)> raycast;
    std::function<glm::vec3(const glm::vec3&)> gravity;
};

struct GhostRayHit {
    std::uint32_t id = 0;
    float fraction = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

class GhostWorld {
public:
    explicit GhostWorld(const GhostData& data) : m_data(&data) {}

    std::uint32_t spawn(GhostTypeId type, const glm::vec3& position, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));
    void tick(float dt, const GhostContext& context, EventList& events);

    const std::vector<Ghost>& ghosts() const { return m_ghosts; }
    const Ghost* find(std::uint32_t id) const;
    const GhostDef& def(const Ghost& ghost) const { return m_data->types[ghost.type]; }
    const GhostData& data() const { return *m_data; }

    std::optional<GhostRayHit> raycast(const glm::vec3& from, const glm::vec3& to) const;

    bool damage(std::uint32_t id, float rounds, DamageKind kind, const glm::vec3& point, EventList& events,
                std::uint32_t source = 0, std::uint8_t attacker = 255);

    void damageArea(const glm::vec3& center, float radius, float rounds, DamageKind kind, EventList& events,
                    std::uint32_t source = 0);

    void reactToShot(const glm::vec3& origin, const glm::vec3& direction, bool dodgeable, EventList& events);

    void push(const glm::vec3& center, float radius, float deltaV);

    void replace(std::vector<Ghost> ghosts) { m_ghosts = std::move(ghosts); }
    void extrapolate(float dt) {
        for (Ghost& ghost : m_ghosts) {
            if (ghost.hitstop <= 0.0f) {
                ghost.position += ghost.velocity * dt;
                ghost.age += dt;
                ghost.stateTime += dt;
            }
        }
    }

    struct Pose {
        GhostState state = GhostState::Wander;
        float loop = 0.0f;
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        bool attached = true;
        float health = -1.0f;
    };
    bool pose(std::uint32_t id, const Pose& pose);
    bool remove(std::uint32_t id);

    void setOn(std::uint32_t id, std::uint8_t player);

    void forget();

private:
    void sense(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt);
    bool tumble(Ghost& ghost, const GhostDef& def, const GhostContext& context, float dt, EventList& events);
    void release(Ghost& ghost, const GhostDef& def);
    void kill(Ghost& ghost, bool burst, EventList& events);

    const GhostData* m_data;
    std::vector<Ghost> m_ghosts;
    std::vector<std::uint32_t> m_dead;
    std::uint32_t m_nextId = 1;
    std::minstd_rand m_rng{4242};

    friend struct GhostAccess;
};

struct GhostAccess {
    GhostWorld& world;
    EventList& events;
    float random01();
    void burst(Ghost& ghost) { world.kill(ghost, true, events); }

    std::vector<Ghost>& ghosts();
    void remove(const Ghost& ghost);
};

std::optional<GhostSurfaceHit> castSurface(const GhostContext& context, const glm::vec3& from, const glm::vec3& to);

struct GhostHitSphere {
    glm::vec3 center{0.0f};
    float radius = 0.3f;
};
GhostHitSphere ghostHitSphere(const Ghost& ghost, const GhostDef& def);

glm::vec3 ghostGravity(const GhostContext& context, const glm::vec3& at);
glm::vec3 ghostUp(const GhostContext& context, const glm::vec3& at);
glm::mat3 upBasis(const glm::vec3& up);
inline float heightOf(const glm::vec3& v, const glm::vec3& up) { return glm::dot(v, up); }
inline glm::vec3 across(const glm::vec3& v, const glm::vec3& up) { return v - up * glm::dot(v, up); }
inline glm::vec3 ringAround(const glm::vec3& up, float angle, float lift = 0.0f) {
    return upBasis(up) * glm::vec3(std::cos(angle), lift, std::sin(angle));
}
bool ghostAttacking(const Ghost& ghost, const GhostDef& def);

inline bool ghostUntouchable(const Ghost& ghost) { return ghost.posed || ghost.state == GhostState::Hop || ghost.state == GhostState::Scatter; }

float vasskrakaSize(const Ghost& ghost, const GhostDef& def);

bool ghostPerceives(const Ghost& ghost, const GhostDef& def, const GhostQuarry& quarry, const GhostContext& context);

}
