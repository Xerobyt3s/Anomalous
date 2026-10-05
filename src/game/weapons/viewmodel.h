#pragma once

#include <glm/glm.hpp>

namespace ghost::game {
struct PlayerState;

struct ViewmodelTuning {
    glm::vec3 hipPosition{0.11f, -0.115f, -0.36f};
    glm::vec3 hipRotationDeg{1.5f, 3.0f, -2.0f};
    float adsDistance = 0.30f;
    glm::vec3 sprintOffset{-0.04f, -0.05f, 0.04f};
    glm::vec3 sprintRotationDeg{-24.0f, 22.0f, 14.0f};

    glm::vec3 crawlOffset{0.02f, -0.08f, 0.06f};
    glm::vec3 crawlRotationDeg{-18.0f, 12.0f, 26.0f};

    glm::vec3 holsterOffset{0.1f, -0.32f, 0.12f};
    glm::vec3 holsterRotationDeg{-60.0f, 15.0f, 30.0f};

    glm::vec3 reloadOffset{-0.07f, 0.03f, 0.07f};
    glm::vec3 reloadRotationDeg{32.0f, -18.0f, -48.0f};
    float reloadBlendRate = 10.0f;

    float aimInTime = 0.22f;
    float aimSwayScale = 0.3f;

    float swayLag = 0.55f;
    float swayMaxDeg = 7.0f;
    float swayStiffness = 140.0f;
    float swayDampingRatio = 0.5f;

    float moveLag = 0.0045f;
    float moveStiffness = 90.0f;
    float moveDampingRatio = 0.6f;
    float strafeTiltDeg = 1.4f;

    float bobAmount = 0.0065f;
    float strideLength = 1.6f;
    float landingKick = 0.012f;

    float kickFlip = 900.0f;
    float kickBack = 2.4f;
    float kickRoll = 140.0f;
    float kickStiffness = 700.0f;
    float kickDampingRatio = 0.55f;
    float settleFlip = 150.0f;
    float settleBack = 0.25f;
    float settleStiffness = 32.0f;
    float settleDampingRatio = 1.0f;
    float kickAimScale = 0.75f;

    float fovHip = 75.0f;
    float fovAds = 55.0f;
    float viewmodelFov = 50.0f;
};

struct SpringVec3 {
    glm::vec3 value{0.0f};
    glm::vec3 velocity{0.0f};

    void update(float dt, float stiffness, float dampingRatio);
};

class Viewmodel {
public:

    void setGunPoints(const glm::vec3& rearSight, const glm::vec3& frontSight, const glm::vec3& grip);

    void update(float dt, const glm::vec2& lookDelta, const PlayerState& player, float walkSpeed, bool reloading);

    glm::mat4 modelToView() const;
    float aimBlend() const;

    void kick(float scale, float rollSign);
    float sprintBlend() const { return m_sprintBlend; }
    float reloadBlend() const { return m_reloadBlend; }

    ViewmodelTuning& tuning() { return m_tuning; }
    void debugUi();

private:
    ViewmodelTuning m_tuning;
    glm::vec3 m_rearSight{0.0f};
    glm::vec3 m_grip{0.0f};
    glm::mat4 m_alignSightLine{1.0f};

    SpringVec3 m_sway;
    SpringVec3 m_shift;
    SpringVec3 m_kickRotation;
    SpringVec3 m_kickOffset;
    SpringVec3 m_settleRotation;
    SpringVec3 m_settleOffset;
    glm::vec3 m_lastViewVelocity{0.0f};
    float m_strafeTilt = 0.0f;
    float m_aimLinear = 0.0f;
    float m_sprintBlend = 0.0f;
    float m_crawlBlend = 0.0f;
    float m_holsterBlend = 0.0f;
    float m_reloadBlend = 0.0f;
    float m_bobPhase = 0.0f;
    float m_bobIntensity = 0.0f;
    float m_time = 0.0f;
    bool m_wasGrounded = true;
    float m_lastVerticalSpeed = 0.0f;
};

}
