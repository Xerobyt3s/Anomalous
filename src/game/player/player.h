#pragma once

#include "engine/physics/physics_world.h"

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace ghost::game {
struct PlayerCommand {
    glm::vec2 move{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool sprint = false;
    bool aim = false;
    bool jump = false;
    bool trigger = false;
    bool cock = false;
    bool handsBusy = false;
    bool interact = false;
    bool crouch = false;
    bool crawl = false;
    bool holster = false;

    glm::vec3 muzzle{0.0f};
    glm::vec3 barrelDirection{0.0f, 0.0f, -1.0f};
};

struct PlayerTuning {
    float walkSpeed = 3.2f;
    float sprintSpeed = 5.8f;
    float aimSpeed = 1.7f;
    float busySpeed = 1.9f;
    float groundAccel = 22.0f;
    float groundDecel = 28.0f;
    float airAccel = 4.0f;
    float jumpSpeed = 4.2f;
    float gravity = 9.81f;
    float eyeHeight = 1.32f;
    float regenDelay = 4.0f;
    float regenRate = 0.12f;
    float radius = 0.3f;
    float height = 1.6f;
    float windAirAccel = 7.0f;
    float liftThreshold = 1.0f;
    float liftCoupling = 4.0f;

    float crouchHeight = 1.24f;
    float crouchEyeHeight = 0.96f;
    float slideEyeHeight = 0.77f;
    float crouchSpeed = 1.6f;
    float stanceRate = 12.0f;
    float slideMinSpeed = 4.5f;
    float slideBoost = 1.1f;
    float slideFriction = 5.0f;
    float slideSteer = 6.0f;
    float slideEndSpeed = 2.0f;
    float slideCooldown = 0.4f;

    float crawlHeight = 0.65f;
    float crawlEyeHeight = 0.42f;
    float crawlSpeed = 1.0f;
    float diveSpeed = 6.5f;
    float diveLift = 1.5f;
    float diveEyeHeight = 0.55f;
    float bellyFriction = 6.0f;
    float rollTime = 0.3f;

    float drawTime = 0.45f;
    float holsterTime = 0.35f;
    float holsteredSpeed = 1.1f;
    float downedHeight = 0.65f;
    float downedEyeHeight = 0.35f;
};

struct PlayerEnvironment {
    glm::vec3 wind{0.0f};

    std::function<engine::CapsuleMove(const glm::vec3& feet, const glm::vec3& velocity, float dt)> move;

    std::function<bool(const glm::vec3& feet, float height)> fits;

    struct Body {
        glm::vec3 feet{0.0f};
        float radius = 0.3f;
        float height = 1.6f;
    };
    std::vector<Body> others;
};

enum class Stance : std::uint8_t { Stand, Crouch, Slide, Crawl, Dive };

struct PlayerState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool grounded = true;
    bool aiming = false;
    bool sprinting = false;

    float hasteTime = 0.0f;
    float hasteScale = 1.0f;
    float shroudTime = 0.0f;
    float health = 1.0f;
    float sinceHurt = 1e3f;
    Stance stance = Stance::Stand;
    float height = 1.6f;
    float eyeHeight = 1.32f;
    float slideCooldown = 0.0f;
    bool crouchHeld = false;
    bool diveUsed = false;
    bool holstered = false;
    float holster = 0.0f;
    float lieYaw = 0.0f;
    float roll = 0.0f;
    bool onBack() const { return std::cos(roll) < 0.0f; }
    bool downed = false;
};

class Player {
public:
    explicit Player(const glm::vec3& spawn = glm::vec3(0.0f));

    void tick(const PlayerCommand& command, float dt, const PlayerEnvironment& environment = {});

    const PlayerState& state() const { return m_state; }
    const PlayerState& previous() const { return m_previous; }
    PlayerTuning& tuning() { return m_tuning; }

    void addImpulse(const glm::vec3& deltaVelocity);
    const PlayerTuning& tuning() const { return m_tuning; }

    void applyHaste(float duration, float speedScale);
    void applyShroud(float duration);
    bool hiddenFromGhosts() const { return m_state.shroudTime > 0.0f; }

    void setVitals(float health, float sinceHurt, bool downed);

    bool hurt(float amount);

    void respawn(const glm::vec3& position);

    void teleport(const glm::vec3& position);

private:
    PlayerTuning m_tuning;
    PlayerState m_state;
    PlayerState m_previous;
};

glm::vec3 flatForward(float yaw);
glm::vec3 flatRight(float yaw);
glm::vec3 viewForward(float yaw, float pitch);

}
