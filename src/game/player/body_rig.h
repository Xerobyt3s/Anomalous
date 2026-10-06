#pragma once

#include "game/player/player.h"
#include "game/weapons/viewmodel.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstdint>

namespace ghost::game {
inline constexpr int kArmPoints = 6;

enum class BodyMode : std::uint8_t {
    Grounded,
    Flung,
    Toppling,
    Down,
    GettingUp,
};

struct Limb {
    glm::vec3 root{0.0f};
    glm::vec3 joint{0.0f};
    glm::vec3 end{0.0f};
};

using Arm = std::array<glm::vec3, kArmPoints>;

struct BodyPose {
    glm::vec3 body{0.0f};
    glm::vec3 bodyRadii{0.22f, 0.31f, 0.15f};
    glm::mat3 bodyBasis{1.0f};
    glm::vec3 head{0.0f};
    glm::vec3 neck{0.0f};
    glm::vec3 headForward{0.0f, 0.0f, -1.0f};
    glm::vec3 headUp{0.0f, 1.0f, 0.0f};
    std::array<Arm, 2> arms{};
    std::array<Limb, 2> legs{};
    std::array<glm::vec3, 2> footForward{glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 0.0f, -1.0f)};
    float stir = 0.0f;
    float blink = 0.0f;
    bool gunFree = false;
    float carry = 0.0f;
    glm::vec3 carryAim{0.0f, -0.77f, -0.64f};
    float holster = 0.0f;
    glm::vec3 holsterGrip{0.0f};
    glm::vec3 holsterAim{0.0f, -1.0f, 0.0f};
    glm::vec3 holsterUp{0.0f, 0.0f, -1.0f};
    glm::vec3 feet{0.0f};
    BodyMode mode = BodyMode::Grounded;
    float holding = 0.0f;
    glm::vec3 held{0.0f};
    float seated = 0.0f;
};

struct BodyInput {
    glm::vec3 feet{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool grounded = true;
    Stance stance = Stance::Stand;
    float height = 1.62f;
    bool downed = false;
    bool aiming = false;
    bool sprinting = false;
    float holster = 0.0f;
    float roll = 0.0f;
    float lieYaw = 0.0f;

    glm::vec3 gunGrip{0.0f};
    glm::vec3 gunFront{0.0f};
    glm::vec3 cylinder{0.0f};
    float crane = 0.0f;
    float ejector = 0.0f;
    float loading = -1.0f;

    bool reach = false;
    glm::vec3 reachTo{0.0f};

    int holdHands = 0;
    float holdHalfWidth = 0.12f;

    float seated = 0.0f;
    float seatYaw = 0.0f;
    glm::vec3 seatHips{0.0f};
    glm::vec3 pedals{0.0f};
    bool steering = false;
    glm::vec3 wheelCenter{0.0f};
    glm::vec3 wheelNormal{0.0f, 0.0f, 1.0f};
    glm::vec3 wheelUp{0.0f, 1.0f, 0.0f};
    float wheelRadius = 0.18f;
    float steer = 0.0f;
    float shifting = 0.0f;
    glm::vec3 shifter{0.0f};
};

struct BodyShape {
    float hip = 0.45f;
    float hipWidth = 0.125f;
    float thigh = 0.23f;
    float shin = 0.23f;
    float radius = 0.22f;
    float depth = 0.15f;
    float core = 0.31f;
    float shoulderWidth = 0.28f;
    float upperArm = 0.27f;
    float forearm = 0.26f;
    float headRadius = 0.26f;
    float neck = 0.035f;
    float armSegment() const { return (upperArm + forearm) / static_cast<float>(kArmPoints - 1); }
};

Limb solveTwoBone(const glm::vec3& root, const glm::vec3& target, float upper, float lower, const glm::vec3& pole);

class BodyRig {
public:
    const BodyPose& update(float dt, const BodyInput& input);
    const BodyPose& pose() const { return m_pose; }
    BodyMode mode() const { return m_mode; }

    void jolt(const glm::vec3& direction, float strength = 1.0f, bool head = false);
    void rebase(const glm::mat3& rotation, const glm::vec3& shift);
    const BodyShape& shape() const { return m_shape; }

    int stepsTaken() const { return m_steps; }
    int lastStepFoot() const { return m_lastStep; }
    float lastStepLength() const { return m_lastStepLength; }

private:
    struct Foot {
        glm::vec3 planted{0.0f};
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        float step = -1.0f;
    };
    using Chain = std::array<glm::vec3, 3>;

    struct JointLimits {
        glm::vec3 coneAxis{0.0f, -1.0f, 0.0f};
        float cone = 1.5f;
        glm::vec3 hingeAxis{1.0f, 0.0f, 0.0f};
        float minBend = 0.0f;
        float maxBend = 2.5f;
    };

    void setMode(BodyMode mode);

    void simulateLimb(Chain& limb, Chain& previous, float upper, float lower, const JointLimits& limits, glm::vec3& hingeMemory, float& bendMemory,
                      float dt, float ground);
    void stepFeet(float dt, const BodyInput& in, const glm::vec3& right, float speed, const glm::vec3& velocity, const glm::vec3& ahead,
                  float width);

    BodyShape m_shape;
    BodyPose m_pose;
    BodyMode m_mode = BodyMode::Grounded;
    float m_modeTime = 0.0f;
    std::array<Foot, 2> m_feet{};
    std::array<Chain, 2> m_armRope{};
    std::array<Chain, 2> m_armsBefore{};
    std::array<Chain, 2> m_legRope{};
    std::array<Chain, 2> m_legRopeBefore{};
    std::array<glm::vec3, 4> m_hinge{};
    std::array<float, 4> m_bend{0.3f, 0.3f, -0.3f, -0.3f};
    bool m_started = false;
    bool m_wasGrounded = true;
    float m_fallSpeed = 0.0f;
    glm::vec3 m_lastVelocity{0.0f};
    float m_stir = 0.0f;
    float m_time = 0.0f;
    int m_lastStep = 1;
    int m_steps = 0;
    float m_lastStepLength = 0.0f;
    float m_hipYaw = 0.0f;
    float m_torsoYaw = 0.0f;
    bool m_turning = false;
    glm::quat m_orient{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 m_spin{0.0f};
    glm::quat m_settleFrom{1.0f, 0.0f, 0.0f, 0.0f};
    float m_settle = 0.0f;
    float m_flop = 0.0f;
    float m_ik = 1.0f;
    SpringVec3 m_lean;
    SpringVec3 m_squash;
    SpringVec3 m_headKick;
    SpringVec3 m_stagger;
    SpringVec3 m_freeHand;
    glm::vec3 m_lastLeanTarget{0.0f};
    glm::vec3 m_lastFreeTarget{0.0f};
    float m_crouch = 0.0f;
    float m_slide = 0.0f;
    float m_swing = 1.0f;
    float m_run = 0.0f;
    float m_runPhase = 0.0f;
    float m_carry = 0.0f;
    float m_lead = 0.0f;
    float m_prone = 0.0f;
    float m_dive = 0.0f;
    float m_crawlPhase = 0.0f;
    float m_handFree = 0.0f;
    float m_reach = 0.0f;
    float m_roll = 0.0f;
    float m_hold = 0.0f;
    float m_twoHand = 0.0f;
};

}
