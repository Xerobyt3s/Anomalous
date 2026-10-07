#include "app/play_view.h"
#include "engine/assets/asset_path.h"
#include "engine/render/post_process.h"
#include "engine/render/primitives.h"
#include "math/glm_bridge.h"
#include "platform/input.h"
#include "player/player.h"
#include "render/camera.h"

#include <glad/glad.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <variant>

namespace anom {
namespace {

constexpr float kRadialHoldTime = 0.18f;
constexpr float kFlickThreshold = 30.0f;
constexpr float kFlickMax = 120.0f;
constexpr std::size_t kMaxDebris = 60;
constexpr float kDebrisLifetime = 30.0f;
constexpr float kDebrisViewmodelTime = 0.45f;
constexpr float kRoundRadius = 0.0061f;
constexpr glm::vec3 kItemOnFoot{0.30f, -0.30f, -0.62f};
constexpr glm::vec3 kItemDriving{0.20f, -0.20f, -0.40f};
constexpr float kItemRaiseRate = 5.0f;
constexpr float kItemLowerRate = 1.0f / 0.3f;

const MechanismState kNoGun{};

void pickRadial(const glm::vec2& flick, const std::vector<ElementId>& options, int& choice)
{
    choice = -1;
    if (glm::length(flick) > kFlickThreshold && !options.empty()) {
        const float n = static_cast<float>(options.size());
        float angle = std::atan2(flick.x, -flick.y);
        if (angle < 0.0f) {
            angle += glm::two_pi<float>();
        }
        choice = static_cast<int>(std::lround(angle / (glm::two_pi<float>() / n))) % static_cast<int>(options.size());
    }
}

}

PlayView::PlayView(Gameplay& play, const engine::PhysicsWorld& physics)
    : m_playMut(play), m_play(play), m_physics(physics), m_ammo(play.ammo()), m_ghosts(play.ghosts()), m_volumes(play.volumes()),
      m_vortices(play.vortices()), m_fog(play.fog()), m_breaths(play.breaths()), m_reveals(play.reveals()),
      m_revealMarks(play.revealMarks()), m_materialDrops(play.materialDrops()), m_dropped(play.dropped()),
      m_ballistics(play.ballistics()), m_fogParams(play.fogParams()), m_fogElement(play.fogElement()),
      m_litShader(engine::assetPath("shaders/ghost_lit.vert"), engine::assetPath("shaders/ghost_lit.frag")),
      m_flashShader(engine::assetPath("shaders/flash.vert"), engine::assetPath("shaders/flash.frag")),
      m_decalShader(engine::assetPath("shaders/fx/decal.vert"), engine::assetPath("shaders/fx/decal.frag")),
      m_carSmokeShader(engine::assetPath("shaders/fx/fog.vert"), engine::assetPath("shaders/fx/car_smoke.frag")),
      m_skidShader(engine::assetPath("shaders/fx/skid.vert"), engine::assetPath("shaders/fx/skid.frag")),
      m_quad(engine::makeQuad()), m_unitBox(engine::makeBox(glm::vec3(1.0f)))
{
    m_fire.setUpField([this](const glm::vec3& at) { return m_play.upAt(at); });
    m_viewmodel.setGunPoints(m_revolver.rearSight(), m_revolver.frontSight(), m_revolver.gripCenter());
    m_revolver.setElements(&m_ammo.elements);
    m_lastChamberTransforms.fill(glm::mat4(1.0f));
    ViewmodelTuning& item = m_itemViewmodel.tuning();
    item.hipPosition = kItemOnFoot;
    item.hipRotationDeg = glm::vec3(0.0f);
    item.holsterOffset = glm::vec3(0.04f, -0.36f, 0.1f);
    item.holsterRotationDeg = glm::vec3(-35.0f, 10.0f, 12.0f);
}

const MechanismState& PlayView::gunState() const
{
    return m_frame.gun ? *m_frame.gun : kNoGun;
}

glm::vec3 PlayView::absorbTarget(PlayerId player) const
{
    if (const PlayerBody* body = m_play.bodyOf(player)) {
        return body->feet + glm::vec3(0.0f, body->height * 0.7f, 0.0f);
    }
    return m_world.position + glm::vec3(0.0f, 1.15f, 0.0f);
}

float PlayView::takeRecoilPitch()
{
    const float pitch = m_pendingPitch;
    m_pendingPitch = 0.0f;
    return pitch;
}

void PlayView::readInput(const Input& input, bool allowed, float frameDt, const PlayFrame& frame, PlayerCommand& pending)
{
    m_frameDt = frameDt;
    const ghost::game::PlayerState& s = frame.state;
    const bool drawnOrDrawing = !s.holstered;
    if (!input.down(MouseButton::Left)) {
        m_triggerBlocked = false;
    }
    if (!allowed || !frame.armed || frame.downed) {
        pending.trigger = false;
        pending.aim = false;
        m_loadHold = -1.0f;
        m_radialOpen = false;
        return;
    }
    if (input.pressed(Key::H) && (frame.canDraw || frame.holdingItem || drawnOrDrawing)) {
        pending.holster = true;
    }
    if (s.holstered && frame.canDraw && (input.pressed(MouseButton::Left) || input.pressed(MouseButton::Right))) {
        pending.holster = true;
        m_triggerBlocked = true;
    }
    pending.aim = input.down(MouseButton::Right) && drawnOrDrawing;
    pending.trigger = !m_triggerBlocked && input.down(MouseButton::Left) && drawnOrDrawing && !m_radialOpen;
    if (s.holstered || s.holster > 0.0f) {
        pending.trigger = false;
        m_loadHold = -1.0f;
        m_radialOpen = false;
        return;
    }
    readLoadSelection(input, frame, pending);
    if (input.pressed(MouseButton::Middle) || input.pressed(Key::Q)) {
        pending.cock = true;
    }
    if (input.pressed(Key::R)) {
        pending.cylinder = true;
    }
    const bool closed = !frame.gun || frame.gun->isClosed();
    if (input.pressed(MouseButton::Right) && !closed) {
        if (m_radialOpen) {
            if (m_radialChoice >= 0) {
                pending.quick_fill_element = static_cast<i32>(m_radialOptions[static_cast<std::size_t>(m_radialChoice)]);
            }
            m_loadHold = -1.0f;
            m_radialOpen = false;
            m_radialChoice = -1;
        } else {
            pending.close_cylinder = true;
        }
    }
    if (input.pressed(Key::F)) {
        pending.eject = true;
    }
    if (input.pressed(Key::G)) {
        pending.speedload = true;
    }
    if (input.scroll() != 0.0f) {
        pending.turn += input.scroll() > 0.0f ? 1 : -1;
    }
}

void PlayView::updateItem(float dt, const PlayFrame& frame)
{
    const bool lowered = !frame.holdingItem || frame.lowering >= 0.0f;
    if (frame.holdingItem && frame.driving != m_itemDriving) {
        m_itemDriving = frame.driving;
        m_itemLower = 1.0f;
    }
    if (!frame.holdingItem) {
        m_itemLower = 1.0f;
    } else if (lowered) {
        m_itemLower = std::min(1.0f, m_itemLower + dt * kItemLowerRate);
    } else {
        m_itemLower += (0.0f - m_itemLower) * std::min(1.0f, kItemRaiseRate * dt);
    }
    m_itemViewmodel.tuning().hipPosition = frame.driving ? kItemDriving : kItemOnFoot;
    ghost::game::PlayerState s = frame.state;
    s.holster = m_itemLower;
    s.holstered = false;
    s.aiming = false;
    if (frame.driving) {
        s.velocity = glm::vec3(0.0f);
        s.sprinting = false;
        s.grounded = true;
    }
    m_itemViewmodel.update(dt, m_lookDelta, s, frame.walkSpeed, false);
}

void PlayView::readLoadSelection(const Input& input, const PlayFrame& frame, PlayerCommand& pending)
{
    if (!frame.gun || frame.gun->isClosed()) {
        m_loadHold = -1.0f;
        m_radialOpen = false;
        return;
    }
    if (input.pressed(MouseButton::Left) && !m_triggerBlocked) {
        m_loadHold = 0.0f;
        m_flick = glm::vec2(0.0f);
        m_radialChoice = -1;
    }
    if (m_loadHold < 0.0f) {
        return;
    }
    if (input.down(MouseButton::Left)) {
        m_loadHold += m_frameDt;
        if (!m_radialOpen && m_loadHold >= kRadialHoldTime) {
            m_radialOptions.clear();
            for (std::size_t e = 0; e < m_ammo.elements.size(); ++e) {
                const auto id = static_cast<ElementId>(e);
                if (id != kPlainElement && frame.pouch && frame.pouch->count(id) > 0) {
                    m_radialOptions.push_back(id);
                }
            }
            m_radialOpen = true;
            m_flick = glm::vec2(0.0f);
        }
        if (m_radialOpen) {
            m_flick += glm::vec2(input.mouse_delta().x, input.mouse_delta().y);
            if (glm::length(m_flick) > kFlickMax) {
                m_flick = glm::normalize(m_flick) * kFlickMax;
            }
            pickRadial(m_flick, m_radialOptions, m_radialChoice);
        }
        return;
    }
    if (!m_radialOpen) {
        if (frame.pouch && frame.pouch->count(kPlainElement) > 0) {
            pending.load_element = static_cast<i32>(kPlainElement);
        }
    } else if (m_radialChoice >= 0) {
        pending.load_element = static_cast<i32>(m_radialOptions[static_cast<std::size_t>(m_radialChoice)]);
    }
    m_loadHold = -1.0f;
    m_radialOpen = false;
}

void PlayView::onEvents(const EventList& events, const PlayFrame& frame, bool client)
{
    m_frame = frame;
    m_local = frame.local;
    m_client = client;
    m_world = frame.world;
    m_eventsIn = &events;
    consumeEvents();
    m_eventsIn = nullptr;
}

void PlayView::update(float dt, const PlayFrame& frame)
{
    m_frame = frame;
    m_local = frame.local;
    m_world = frame.world;
    m_frameDt = dt;
    if (m_lookValid) {
        m_lookDelta = glm::vec2(std::remainder(frame.state.yaw - m_lastYaw, glm::two_pi<float>()), frame.state.pitch - m_lastPitch);
    }
    m_lastYaw = frame.state.yaw;
    m_lastPitch = frame.state.pitch;
    m_lookValid = true;
    m_viewmodel.update(dt, m_lookDelta, frame.state, frame.walkSpeed, frame.gun && !frame.gun->isClosed());
    updateItem(dt, frame);
    updateDebris(dt);
    updatePuffs(dt);
    replayPosed();
    updateElementFx(dt, 1.0f);
    updateAudio(dt);
    m_cameraKick.update(dt, m_cameraRecoil.stiffness, m_cameraRecoil.dampingRatio);
    m_fovPunch.update(dt, 300.0f, 0.6f);
    for (ImpactFlash& f : m_impactFlashes) {
        f.age += dt;
    }
    std::erase_if(m_impactFlashes, [](const ImpactFlash& f) { return f.age >= f.duration; });
    m_flashTime -= dt;
}

void PlayView::adjustCamera(Camera& camera, const PlayFrame& frame, float dt)
{
    m_cameraPos = to_glm(camera.pos);
    if (!frame.armed) {
        m_worldFovDeg = glm::degrees(camera.fov_y);
        return;
    }
    const ViewmodelTuning& t = m_viewmodel.tuning();
    const CameraRecoilTuning& r = m_cameraRecoil;
    glm::vec3 shake{0.0f};
    if (m_shakeTime > 0.0f) {
        const float strength = m_shakeTime / r.shakeTime;
        const float phase = (r.shakeTime - m_shakeTime) + m_shakeSeed;
        shake = glm::vec3(std::sin(phase * 91.0f), std::sin(phase * 77.0f + 1.3f), std::sin(phase * 103.0f + 2.1f)) * r.shakeDeg *
                strength * strength;
        m_shakeTime -= dt;
    }
    shake += glm::vec3(std::sin(m_fxTime * 63.0f), std::sin(m_fxTime * 71.0f + 2.0f), std::sin(m_fxTime * 57.0f + 4.0f) * 0.6f) *
             m_blastShake;
    m_worldFovDeg = glm::mix(t.fovHip, t.fovAds, m_viewmodel.aimBlend()) + m_fovPunch.value.x + m_selfFx.haste * m_selfFxTuning.hasteFov;
    camera.yaw += glm::radians(m_cameraKick.value.y + shake.y);
    camera.pitch = std::clamp(camera.pitch + glm::radians(m_cameraKick.value.x + shake.x), glm::radians(-89.0f), glm::radians(89.0f));
    camera.roll += glm::radians(m_cameraKick.value.z + shake.z);
    camera.fov_y = glm::radians(m_worldFovDeg);
}

void PlayView::prepareGun(const Camera& camera, float aspect)
{
    m_viewport = glm::ivec2(static_cast<int>(aspect * 1000.0f), 1000);
    m_view = to_glm(camera.view());
    const glm::mat4 invView = glm::inverse(m_view);
    m_gunToWorld = invView * m_viewmodel.modelToView();
    const ViewmodelTuning& t = m_viewmodel.tuning();
    const float k = std::tan(glm::radians(m_worldFovDeg) * 0.5f) / std::tan(glm::radians(t.viewmodelFov) * 0.5f);
    const auto remap = [&](const glm::vec3& point, float factor) {
        glm::vec4 v = m_view * glm::vec4(point, 1.0f);
        v.z *= factor;
        return glm::vec3(invView * v);
    };
    const glm::vec3 muzzleGunLayer = glm::vec3(m_gunToWorld * glm::vec4(m_revolver.muzzle(), 1.0f));
    m_lastMuzzleWorld = remap(muzzleGunLayer, 1.0f / k);
    m_lastGunForward = glm::normalize(glm::vec3(m_gunToWorld * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
    const glm::vec3 boreFar = muzzleGunLayer + m_lastGunForward * 40.0f;
    m_aimOrigin = m_lastMuzzleWorld;
    m_aimDirection = glm::normalize(remap(boreFar, 1.0f / k) - m_lastMuzzleWorld);
}

void PlayView::renderWorld(const glm::mat4& viewProj, const glm::vec3& cameraPos, const FrameLights& lights)
{
    m_lightDir = lights.sunDirection;
    setFrameLights(m_lastMuzzleWorld);
    m_litShader.use();
    m_litShader.set("uViewProj", viewProj);
    m_litShader.set("uCameraPos", cameraPos);
    m_litShader.set("uGhost", 0.0f);
    m_litShader.set("uUseMaps", 0);
    for (const ImpactMark& mark : m_marks) {
        const bool steel = mark.surface == Surface::Steel;
        if (!steel) {
            continue;
        }
        glm::vec3 point = mark.point;
        glm::vec3 normal = mark.normal;
        toWorldSpace(mark.body, mark.attached, point, normal);
        m_litShader.set("uBaseColor", steel ? glm::vec3(0.75f) : glm::vec3(0.02f));
        m_litShader.set("uMetallic", steel ? 1.0f : 0.0f);
        m_litShader.set("uRoughness", steel ? 0.4f : 1.0f);
        m_litShader.set("uHighlight", 0.0f);
        m_litShader.set("uEmissive", glm::vec3(0.0f));
        const glm::vec3 helper = std::abs(normal.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 x = glm::normalize(glm::cross(helper, normal));
        const glm::vec3 y = glm::cross(normal, x);
        const glm::mat4 facing(glm::vec4(x, 0.0f), glm::vec4(y, 0.0f), glm::vec4(normal, 0.0f), glm::vec4(0, 0, 0, 1));
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), point + normal * 0.002f) * facing *
                                glm::rotate(glm::mat4(1.0f), mark.spin, glm::vec3(0.0f, 0.0f, 1.0f)) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(steel ? 0.02f : 0.014f));
        m_litShader.set("uModel", model);
        m_litShader.set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        m_quad.draw();
    }
    for (const Debris& d : m_debris) {
        if (!d.viewmodelLayer) {
            m_revolver.drawRound(m_litShader, debrisModel(d), ChamberState::Spent, d.element);
        }
    }
    for (const DroppedRound& drop : m_dropped) {
        const glm::mat4 model = glm::translate(glm::mat4(1.0f), drop.position) *
                                glm::rotate(glm::mat4(1.0f), drop.yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                                glm::translate(glm::mat4(1.0f), -m_revolver.roundCenter());
        m_revolver.drawRound(m_litShader, model, ChamberState::Live, drop.round.element);
    }
}

void PlayView::renderOtherGuns(const std::vector<OtherGun>& guns, const glm::mat4& viewProj, const glm::vec3& cameraPos)
{
    if (guns.empty()) {
        return;
    }
    m_litShader.use();
    m_litShader.set("uViewProj", viewProj);
    m_litShader.set("uCameraPos", cameraPos);
    m_litShader.set("uUseMaps", 0);
    for (const OtherGun& gun : guns) {
        m_litShader.set("uGhost", gun.shroud);
        m_litShader.set("uGhostTime", m_fxTime);
        m_revolver.render(m_litShader, gun.model, gun.view);
    }
    m_litShader.set("uGhost", 0.0f);
}

void PlayView::renderEffects(const glm::mat4& viewProj, const Camera& camera, float alpha, std::span<const SeeThrough> extras)
{
    m_extras = extras;
    const glm::vec3 cameraPos = to_glm(camera.pos);
    const glm::mat4 invView = glm::inverse(to_glm(camera.view()));
    m_cameraPos = cameraPos;
    drawMimics(viewProj, cameraPos);
    drawFlocks(viewProj, cameraPos);
    drawTracers(viewProj, alpha);
    if (m_post) {
        m_sceneDepthCopy = m_post->copySceneDepth();
    }
    drawDecals(viewProj);
    drawSeeThrough(viewProj, invView, cameraPos, to_glm(camera.forward()));
    drawRevealMarks(viewProj, invView, cameraPos, to_glm(camera.forward()));
    m_extras = {};
    engine::PostProcess::useWorldDepthRange();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}

void PlayView::finishFrame(const glm::mat4& viewProj, const glm::vec3& eye)
{
    gatherDistortion(viewProj, eye);
}

void PlayView::renderGun(const Camera& camera, float aspect, const PlayFrame& frame, const FrameLights& lights)
{
    m_gunVisible = frame.armed && frame.state.holster < 0.99f;
    const ViewmodelTuning& t = m_viewmodel.tuning();
    const glm::mat4 invView = glm::inverse(m_view);
    const float k = std::tan(glm::radians(m_worldFovDeg) * 0.5f) / std::tan(glm::radians(t.viewmodelFov) * 0.5f);
    const auto remap = [&](const glm::vec3& point, float factor) {
        glm::vec4 v = m_view * glm::vec4(point, 1.0f);
        v.z *= factor;
        return glm::vec3(invView * v);
    };
    const auto shifted = [](const glm::mat4& model, const glm::vec3& from, const glm::vec3& to) {
        return glm::translate(glm::mat4(1.0f), to - from) * model;
    };

    engine::PostProcess::useNearDepthRange();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    const glm::mat4 viewmodelViewProj = glm::perspective(glm::radians(t.viewmodelFov), aspect, 0.01f, 10.0f) * m_view;
    m_lightDir = lights.sunDirection;
    setFrameLights(m_lastMuzzleWorld);
    m_litShader.use();
    m_litShader.set("uViewProj", viewmodelViewProj);
    m_litShader.set("uCameraPos", to_glm(camera.pos));
    m_litShader.set("uGhost", m_selfFx.shroud * m_selfFxTuning.gunDissolve);
    m_litShader.set("uGhostTime", m_fxTime);
    if (m_gunVisible) {
        m_revolver.render(m_litShader, m_gunToWorld, frame.mechanism);
    }
    m_litShader.set("uGhost", 0.0f);
    drawGunArcs(viewmodelViewProj, to_glm(camera.pos), m_gunToWorld);
    m_litShader.use();
    for (const Debris& d : m_debris) {
        if (d.viewmodelLayer) {
            m_revolver.drawRound(m_litShader, shifted(debrisModel(d), d.position, remap(d.position, k)), ChamberState::Spent, d.element);
        }
    }
    const std::array<glm::mat4, kChamberCount> chambers = m_revolver.chamberTransforms(m_gunToWorld, frame.mechanism);
    for (std::size_t i = 0; i < chambers.size(); ++i) {
        const glm::vec3 center = glm::vec3(chambers[i] * glm::vec4(m_revolver.roundCenter(), 1.0f));
        m_lastChamberTransforms[i] = shifted(chambers[i], center, remap(center, 1.0f / k));
    }
    trackChambers(m_frameDt);

    if (m_flashTime > 0.0f && m_gunVisible) {
        const float flashStrength = std::max(m_flashTime, 0.0f) / m_effects.flashTime;
        const glm::vec3 muzzle = glm::vec3(m_gunToWorld * glm::vec4(m_revolver.muzzle(), 1.0f));
        const glm::vec3 gap = glm::vec3(m_gunToWorld * glm::vec4(m_revolver.cylinderGap(), 1.0f));
        const glm::vec3 forward = glm::normalize(glm::vec3(m_gunToWorld * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        const float size = m_effects.flashSize * (0.85f + 0.3f * flashStrength) * (m_flashElectric ? 0.4f : 1.0f);
        const glm::vec3 outer = m_flashElectric ? glm::vec3(0.45f, 0.6f, 1.0f) : glm::vec3(1.0f, 0.72f, 0.38f);
        const glm::vec3 inner = m_flashElectric ? glm::vec3(0.85f, 0.92f, 1.0f) : glm::vec3(1.0f, 0.95f, 0.8f);
        const glm::vec3 side = m_flashElectric ? glm::vec3(0.5f, 0.65f, 1.0f) : glm::vec3(1.0f, 0.6f, 0.3f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        m_flashShader.use();
        drawFlash(viewmodelViewProj, invView, muzzle + forward * size * 0.45f, size, outer, 3.0f * flashStrength, m_flashSeed, 1.0f);
        drawFlash(viewmodelViewProj, invView, muzzle + forward * size * 0.15f, size * 0.45f, inner, 2.5f * flashStrength,
                  m_flashSeed + 1.7f, 0.0f);
        drawFlash(viewmodelViewProj, invView, gap, size * 0.35f, side, 1.6f * flashStrength, m_flashSeed + 3.1f, 1.0f);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    engine::PostProcess::useWorldDepthRange();
}

void PlayView::drawFlash(const glm::mat4& viewProj, const glm::mat4& invView, const glm::vec3& center, float size,
                         const glm::vec3& color, float intensity, float seed, float spikes)
{
    if (!inWindow(center)) {
        return;
    }
    drawFlashAxes(viewProj, center, glm::vec3(invView[0]), glm::vec3(invView[1]), size, color, intensity, seed, spikes, 0.0f);
}

void PlayView::trackChambers(float dt)
{
    for (std::size_t i = 0; i < m_lastChamberTransforms.size(); ++i) {
        const glm::vec3 center = glm::vec3(m_lastChamberTransforms[i] * glm::vec4(m_revolver.roundCenter(), 1.0f));
        glm::vec3 velocity{0.0f};
        if (m_chambersTracked && dt > 1e-4f) {
            velocity = (center - m_chamberCenters[i]) / dt;
            const float speed = glm::length(velocity);
            if (speed > 3.0f) {
                velocity *= 3.0f / speed;
            }
        }
        m_chamberVelocities[i] = glm::mix(m_chamberVelocities[i], velocity, 0.6f);
        m_chamberCenters[i] = center;
    }
    m_chambersTracked = true;
}

glm::mat4 PlayView::debrisModel(const Debris& d) const
{
    return glm::translate(glm::mat4(1.0f), d.position) * glm::mat4_cast(d.rotation) * glm::translate(glm::mat4(1.0f), -d.spawnCenter) *
           d.spawn;
}

void PlayView::updateDebris(float dt)
{
    for (Debris& d : m_debris) {
        d.age += dt;
        if (d.viewmodelLayer && d.age > kDebrisViewmodelTime) {
            d.viewmodelLayer = false;
        }
        const glm::vec3 air = airVelocity(m_vortices, d.position);
        if (glm::dot(air, air) > 0.09f && !d.viewmodelLayer) {
            d.resting = false;
            d.velocity += (air - d.velocity) * std::min(5.0f * dt, 1.0f);
            d.spin = glm::vec3(air.z, 2.0f, -air.x) * 2.0f;
        }
        if (d.resting) {
            continue;
        }
        d.velocity += m_play.gravityAt(d.position) * dt;
        const glm::vec3 next = d.position + d.velocity * dt;
        if (const auto hit = m_physics.raycast(d.position, next)) {
            d.position = hit->point + hit->normal * kRoundRadius;
            if (glm::length(d.velocity) > 1.0f) {
                m_audio.play("casing.drop", SoundGroup::World, std::min(glm::length(d.velocity) / 4.0f, 1.0f), d.position);
            }
            d.velocity = glm::reflect(d.velocity, hit->normal) * 0.35f;
            d.spin *= 0.5f;
            if (glm::length(d.velocity) < 0.25f) {
                d.resting = true;
                const glm::vec3 axis = glm::vec3(glm::mat4_cast(d.rotation) * d.spawn * glm::vec4(0, 0, 1, 0));
                const float yaw = std::atan2(axis.x, axis.z);
                d.rotation = glm::quat(glm::vec3(0.0f, 1.0f, 0.0f), m_play.upAt(d.position));
                d.spawn = glm::translate(glm::mat4(1.0f), d.spawnCenter) * glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                          glm::translate(glm::mat4(1.0f), -m_revolver.roundCenter());
            }
        } else {
            d.position = next;
            const float angle = glm::length(d.spin) * dt * std::min(1.0f, d.age / 0.25f);
            if (angle > 0.0f) {
                d.rotation = glm::angleAxis(angle, glm::normalize(d.spin)) * d.rotation;
            }
        }
    }
    std::erase_if(m_debris, [](const Debris& d) { return d.age > kDebrisLifetime; });
}

}
