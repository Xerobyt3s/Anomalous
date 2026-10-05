#define GLM_ENABLE_EXPERIMENTAL

#include "game/weapons/viewmodel.h"

#include "game/player/player.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
glm::mat4 rotationDeg(const glm::vec3& eulerDeg) { return glm::mat4_cast(glm::quat(glm::radians(eulerDeg))); }

float expApproach(float current, float target, float rate, float dt) {
    return target + (current - target) * std::exp(-rate * dt);
}

}

void SpringVec3::update(float dt, float stiffness, float dampingRatio) {
    constexpr float kMaxStep = 1.0f / 240.0f;
    const float damping = 2.0f * dampingRatio * std::sqrt(stiffness);
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / kMaxStep)));
    const float h = dt / static_cast<float>(steps);
    for (int i = 0; i < steps; ++i) {
        velocity += (-stiffness * value - damping * velocity) * h;
        value += velocity * h;
    }
}

void Viewmodel::setGunPoints(const glm::vec3& rearSight, const glm::vec3& frontSight, const glm::vec3& grip) {
    m_rearSight = rearSight;
    m_grip = grip;

    const glm::mat4 turn = glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 sightDir = glm::normalize(glm::vec3(turn * glm::vec4(frontSight - rearSight, 0.0f)));
    m_alignSightLine = glm::mat4_cast(glm::rotation(sightDir, glm::vec3(0.0f, 0.0f, -1.0f))) * turn;
}

void Viewmodel::update(float dt, const glm::vec2& lookDelta, const PlayerState& player, float walkSpeed,
                       bool reloading) {
    dt = std::min(dt, 1.0f / 20.0f);
    const ViewmodelTuning& t = m_tuning;
    m_time += dt;

    const float aimStep = dt / std::max(t.aimInTime, 0.01f);
    m_aimLinear = std::clamp(m_aimLinear + (player.aiming ? aimStep : -aimStep), 0.0f, 1.0f);
    m_sprintBlend = expApproach(m_sprintBlend, player.sprinting ? 1.0f : 0.0f, 9.0f, dt);
    const bool crawling = player.stance == Stance::Crawl && !player.onBack() && !player.aiming && glm::length(glm::vec2(player.velocity.x, player.velocity.z)) > 0.3f;
    m_crawlBlend = expApproach(m_crawlBlend, crawling ? 1.0f : 0.0f, 7.0f, dt);
    m_holsterBlend = player.holster * player.holster * (3.0f - 2.0f * player.holster);
    m_reloadBlend = expApproach(m_reloadBlend, reloading ? 1.0f : 0.0f, t.reloadBlendRate, dt);

    const float maxSway = glm::radians(t.swayMaxDeg);
    m_sway.value.y = std::clamp(m_sway.value.y + lookDelta.x * t.swayLag, -maxSway, maxSway);
    m_sway.value.x = std::clamp(m_sway.value.x - lookDelta.y * t.swayLag, -maxSway, maxSway);
    m_sway.update(dt, t.swayStiffness, t.swayDampingRatio);

    const glm::vec3 forward = flatForward(player.yaw);
    const glm::vec3 right = flatRight(player.yaw);
    const glm::vec3 viewVelocity{glm::dot(player.velocity, right), player.velocity.y,
                                 -glm::dot(player.velocity, forward)};
    glm::vec3 deltaV = viewVelocity - m_lastViewVelocity;
    deltaV.y = 0.0f;
    m_shift.value -= deltaV * t.moveLag;
    m_shift.value = glm::clamp(m_shift.value, glm::vec3(-0.03f), glm::vec3(0.03f));
    m_lastViewVelocity = viewVelocity;

    if (player.grounded && !m_wasGrounded) {
        m_shift.velocity.y -= std::abs(m_lastVerticalSpeed) * t.landingKick * 10.0f;
    }
    m_wasGrounded = player.grounded;
    m_lastVerticalSpeed = player.velocity.y;
    m_shift.update(dt, t.moveStiffness, t.moveDampingRatio);
    m_kickRotation.update(dt, t.kickStiffness, t.kickDampingRatio);
    m_kickOffset.update(dt, t.kickStiffness, t.kickDampingRatio);
    m_settleRotation.update(dt, t.settleStiffness, t.settleDampingRatio);
    m_settleOffset.update(dt, t.settleStiffness, t.settleDampingRatio);

    m_strafeTilt = expApproach(m_strafeTilt, -viewVelocity.x * t.strafeTiltDeg, 8.0f, dt);

    const float horizontalSpeed = glm::length(glm::vec2(player.velocity.x, player.velocity.z));
    const float targetIntensity = player.grounded ? std::min(horizontalSpeed / walkSpeed, 1.6f) : 0.0f;
    m_bobIntensity = expApproach(m_bobIntensity, targetIntensity, 10.0f, dt);
    m_bobPhase += horizontalSpeed / t.strideLength * glm::pi<float>() * dt;
}

void Viewmodel::kick(float scale, float rollSign) {
    const ViewmodelTuning& t = m_tuning;
    const float s = scale * glm::mix(1.0f, t.kickAimScale, aimBlend());
    m_kickRotation.velocity += glm::vec3(t.kickFlip, 0.0f, t.kickRoll * rollSign) * s;
    m_kickOffset.velocity.z += t.kickBack * s;
    m_settleRotation.velocity.x += t.settleFlip * s;
    m_settleOffset.velocity.z += t.settleBack * s;
}

float Viewmodel::aimBlend() const {
    return m_aimLinear * m_aimLinear * (3.0f - 2.0f * m_aimLinear);
}

glm::mat4 Viewmodel::modelToView() const {
    const ViewmodelTuning& t = m_tuning;
    const float aim = aimBlend();
    const float steady = glm::mix(1.0f, t.aimSwayScale, aim);

    const glm::vec3 position = glm::mix(t.hipPosition, glm::vec3(0.0f, 0.0f, -t.adsDistance), aim) +
                               t.sprintOffset * m_sprintBlend + t.crawlOffset * m_crawlBlend + t.reloadOffset * m_reloadBlend + t.holsterOffset * m_holsterBlend;
    const glm::vec3 rotation = glm::mix(t.hipRotationDeg, glm::vec3(0.0f), aim) + t.sprintRotationDeg * m_sprintBlend + t.crawlRotationDeg * m_crawlBlend + t.holsterRotationDeg * m_holsterBlend +
                               t.reloadRotationDeg * m_reloadBlend;
    const glm::mat4 base = glm::translate(glm::mat4(1.0f), position) * rotationDeg(rotation) * m_alignSightLine *
                           glm::translate(glm::mat4(1.0f), -m_rearSight);

    const float bob = t.bobAmount * m_bobIntensity * steady;
    const glm::vec3 bobOffset{std::sin(m_bobPhase) * bob * 0.6f, (std::cos(2.0f * m_bobPhase) - 1.0f) * 0.5f * bob,
                              0.0f};
    const glm::vec3 breathe{0.0f, std::sin(m_time * 1.6f) * 0.0010f, 0.0f};
    const glm::vec3 offset = m_shift.value * steady + bobOffset + breathe * steady;

    const glm::vec3 swayDeg{glm::degrees(m_sway.value.x) + std::sin(m_time * 0.9f) * 0.12f,
                            glm::degrees(m_sway.value.y),
                            -glm::degrees(m_sway.value.y) * 0.5f + m_strafeTilt};

    const glm::vec3 gripView = glm::vec3(base * glm::vec4(m_grip, 1.0f));

    return glm::translate(glm::mat4(1.0f), offset + m_kickOffset.value + m_settleOffset.value + gripView) *
           rotationDeg(swayDeg * steady + m_kickRotation.value + m_settleRotation.value) *
           glm::translate(glm::mat4(1.0f), -gripView) * base;
}

void Viewmodel::debugUi() {
    ViewmodelTuning& t = m_tuning;
    ImGui::SetNextWindowPos({10.0f, 330.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Viewmodel");
    ImGui::Text("Aim %.2f  Sprint %.2f", aimBlend(), m_sprintBlend);
    if (ImGui::CollapsingHeader("Poses", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Hip position", &t.hipPosition.x, 0.001f, -1.0f, 1.0f, "%.3f");
        ImGui::DragFloat3("Hip rotation", &t.hipRotationDeg.x, 0.1f, -45.0f, 45.0f, "%.1f");
        ImGui::DragFloat("ADS distance", &t.adsDistance, 0.002f, 0.1f, 0.8f, "%.3f");
        ImGui::DragFloat3("Sprint offset", &t.sprintOffset.x, 0.001f, -0.5f, 0.5f, "%.3f");
        ImGui::DragFloat3("Sprint rotation", &t.sprintRotationDeg.x, 0.1f, -90.0f, 90.0f, "%.1f");
        ImGui::DragFloat3("Crawl offset", &t.crawlOffset.x, 0.001f, -0.5f, 0.5f, "%.3f");
        ImGui::DragFloat3("Crawl rotation", &t.crawlRotationDeg.x, 0.1f, -90.0f, 90.0f, "%.1f");
        ImGui::DragFloat3("Holster offset", &t.holsterOffset.x, 0.001f, -0.6f, 0.6f, "%.3f");
        ImGui::DragFloat3("Holster rotation", &t.holsterRotationDeg.x, 0.1f, -90.0f, 90.0f, "%.1f");
        ImGui::DragFloat3("Reload offset", &t.reloadOffset.x, 0.001f, -0.5f, 0.5f, "%.3f");
        ImGui::DragFloat3("Reload rotation", &t.reloadRotationDeg.x, 0.1f, -90.0f, 90.0f, "%.1f");
        ImGui::SliderFloat("Aim time", &t.aimInTime, 0.05f, 0.6f, "%.2f s");
        ImGui::SliderFloat("Aim steadiness", &t.aimSwayScale, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Sway & inertia", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Look lag", &t.swayLag, 0.0f, 1.5f);
        ImGui::SliderFloat("Max sway (deg)", &t.swayMaxDeg, 0.0f, 20.0f);
        ImGui::SliderFloat("Sway stiffness", &t.swayStiffness, 10.0f, 600.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Sway damping", &t.swayDampingRatio, 0.1f, 2.0f);
        ImGui::SliderFloat("Move lag", &t.moveLag, 0.0f, 0.02f, "%.4f");
        ImGui::SliderFloat("Move stiffness", &t.moveStiffness, 10.0f, 600.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Move damping", &t.moveDampingRatio, 0.1f, 2.0f);
        ImGui::SliderFloat("Strafe tilt", &t.strafeTiltDeg, 0.0f, 5.0f);
        ImGui::SliderFloat("Bob", &t.bobAmount, 0.0f, 0.03f, "%.4f");
        ImGui::SliderFloat("Stride", &t.strideLength, 0.5f, 3.0f);
        ImGui::SliderFloat("Landing kick", &t.landingKick, 0.0f, 0.05f, "%.3f");
    }
    if (ImGui::CollapsingHeader("Recoil (gun)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Snap flip", &t.kickFlip, 0.0f, 3000.0f, "%.0f");
        ImGui::SliderFloat("Push back", &t.kickBack, 0.0f, 4.0f);
        ImGui::SliderFloat("Roll", &t.kickRoll, 0.0f, 300.0f, "%.0f");
        ImGui::SliderFloat("Snap stiffness", &t.kickStiffness, 20.0f, 2000.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Snap damping", &t.kickDampingRatio, 0.1f, 2.0f);
        ImGui::SliderFloat("Settle flip", &t.settleFlip, 0.0f, 600.0f, "%.0f");
        ImGui::SliderFloat("Settle push", &t.settleBack, 0.0f, 2.0f);
        ImGui::SliderFloat("Settle stiffness", &t.settleStiffness, 5.0f, 200.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Settle damping", &t.settleDampingRatio, 0.3f, 2.0f);
        ImGui::SliderFloat("Aimed scale", &t.kickAimScale, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("FOV")) {
        ImGui::SliderFloat("World hip", &t.fovHip, 50.0f, 110.0f, "%.0f");
        ImGui::SliderFloat("World ADS", &t.fovAds, 20.0f, 90.0f, "%.0f");
        ImGui::SliderFloat("Viewmodel", &t.viewmodelFov, 30.0f, 90.0f, "%.0f");
    }
    ImGui::End();
}

}
