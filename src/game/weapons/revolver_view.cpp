#include "game/weapons/revolver_view.h"

#include "engine/assets/asset_path.h"
#include "engine/debug/log.h"
#include "engine/render/primitives.h"
#include "engine/render/shader.h"
#include "engine/render/texture.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace ghost::game {
namespace {
constexpr float kChamberStep = glm::two_pi<float>() / kChamberCount;

constexpr glm::vec3 kCylinderAxis{0.0f, 0.0f, 1.0f};
constexpr glm::vec3 kPartAxisX{1.0f, 0.0f, 0.0f};

struct SurfaceMaterial {
    glm::vec3 baseColor;
    float metallic;
    float roughness;
};

SurfaceMaterial materialFor(std::string_view part) {
    if (part == "Grip") {
        return {{0.11f, 0.05f, 0.022f}, 0.0f, 0.5f};
    }
    if (part == "Casing") {
        return {{0.92f, 0.68f, 0.32f}, 1.0f, 0.3f};
    }
    if (part == "Bullet") {
        return {{0.55f, 0.55f, 0.57f}, 1.0f, 0.55f};
    }
    return {{0.28f, 0.29f, 0.31f}, 1.0f, 0.32f};
}

void drawPart(const engine::Shader& shader, const engine::Mesh& mesh, std::string_view name,
              const glm::mat4& model, bool highlighted, const glm::vec3& tint = glm::vec3(1.0f),
              const glm::vec3& emissive = glm::vec3(0.0f), const TexturedMaterial* surface = nullptr,
              const glm::mat4& mapSpace = glm::mat4(1.0f)) {
    const SurfaceMaterial material = materialFor(name);
    shader.set("uBaseColor", material.baseColor * tint);
    shader.set("uMetallic", material.metallic);
    shader.set("uRoughness", material.roughness);
    shader.set("uHighlight", highlighted ? 1.0f : 0.0f);
    shader.set("uEmissive", emissive);
    shader.set("uModel", model);
    shader.set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    bindMaterial(shader, surface, model, mapSpace, tint);
    mesh.draw();
    shader.set("uUseMaps", 0);
}

}

RevolverView::RevolverView()
    : m_revolver(engine::loadGltf(engine::assetPath("models/Revolver.glb"))),
      m_round(engine::loadGltf(engine::assetPath("models/Bullet.glb"))),
      m_revolverMeshes(engine::uploadMeshes(m_revolver)), m_roundMeshes(engine::uploadMeshes(m_round)),
      m_slotRing(engine::makeRing(0.0064f, 0.0079f)),
      m_bindWorld(m_revolver.computeWorld(m_revolver.bindLocals())),
      m_roundWorld(m_round.computeWorld(m_round.bindLocals())), m_poses(m_revolver.nodes.size()),
      m_cylinder(m_revolver.findNode("Cylinder")), m_hammer(m_revolver.findNode("Hammer")),
      m_trigger(m_revolver.findNode("Trigger")), m_crane(m_revolver.findNode("Crane")),
      m_ejectRod(m_revolver.findNode("EjectRod")), m_casingNode(m_round.findNode("Casing")) {
    m_walnut = loadMaterial("walnut", 6.0f, glm::vec3(1.05f, 0.72f, 0.5f), 0.6f, 3.0f);

    m_steel = loadMaterial("steel", 5.0f, glm::vec3(0.55f, 0.58f, 0.66f), 0.85f, 1.0f);
    m_steel.contrast = 0.35f;
    m_steel.blendSharpness = 1.5f;
    m_walnut.blendSharpness = 2.5f;
    m_brass = loadMaterial("brass", 25.0f, glm::vec3(1.0f, 0.82f, 0.6f), 1.0f, 1.0f);
    m_lead = loadMaterial("lead", 25.0f, glm::vec3(0.75f), 1.1f, 1.0f);
    if (m_cylinder >= 0) {
        m_roundToCylinder = glm::inverse(m_bindWorld[m_cylinder]);

        const int mesh = m_revolver.nodes[m_cylinder].mesh;
        if (mesh >= 0) {
            const engine::MeshData& data = m_revolver.meshes[mesh];
            const float offAxis = glm::length(glm::vec2((data.boundsMin + data.boundsMax) * 0.5f));
            if (offAxis > 0.0005f) {
                GHOST_WARN("Cylinder origin is {:.1f} mm off its axis; it will rotate around the wrong point "
                           "(fix: Set Origin > Origin to Geometry, Bounds Center)",
                           offAxis * 1000.0f);
            }
        }
    } else {
        GHOST_WARN("Revolver.glb has no 'Cylinder' node; rounds won't be shown");
    }

    const glm::vec3 center{0.5f};
    const glm::vec3 top{0.5f, 1.0f, 0.5f};

    m_rearSight = meshPoint(m_revolver, m_bindWorld, "SightFinder", top, {0.0f, 0.1f, 0.05f});
    m_frontSight = meshPoint(m_revolver, m_bindWorld, "sightEnd", top, {0.0f, 0.1f, 0.27f});
    m_gripCenter = meshPoint(m_revolver, m_bindWorld, "Grip", center, glm::vec3(0.0f));

    const glm::vec3 bore = meshPoint(m_round, m_roundWorld, "Casing", center, {0.0f, 0.087f, 0.08f});
    const glm::vec3 barrelFront = meshPoint(m_revolver, m_bindWorld, "Barrel", {0.5f, 0.5f, 1.0f}, {0, 0, 0.28f});
    m_muzzle = {bore.x, bore.y, barrelFront.z};

    m_steel.soot = 0.8f;
    m_steel.sootFrom = m_gripCenter;
    m_steel.sootTo = m_muzzle;

    const glm::vec3 cylinderFront = meshPoint(m_revolver, m_bindWorld, "Cylinder", {0.5f, 0.5f, 1.0f}, {0, 0.07f, 0.11f});
    m_cylinderGap = {bore.x, bore.y, cylinderFront.z + 0.002f};
    m_roundCenter = bore;
    m_roundBase = meshPoint(m_round, m_roundWorld, "Casing", {0.5f, 0.5f, 0.0f}, bore);
}

glm::vec3 RevolverView::meshPoint(const engine::ModelData& model, const std::vector<glm::mat4>& world,
                                  const char* name, const glm::vec3& boundsWeight, const glm::vec3& fallback) const {
    const int node = model.findNode(name);
    if (node < 0 || model.nodes[node].mesh < 0) {
        GHOST_WARN("No mesh node '{}', using a guessed position", name);
        return fallback;
    }
    const engine::MeshData& mesh = model.meshes[model.nodes[node].mesh];
    const glm::vec3 local = glm::mix(mesh.boundsMin, mesh.boundsMax, boundsWeight);
    return glm::vec3(world[node] * glm::vec4(local, 1.0f));
}

std::vector<glm::mat4> RevolverView::posedLocals(const MechanismView& mechanism) const {
    std::vector<glm::mat4> locals = m_revolver.bindLocals();
    auto rotateLocal = [&](int node, float radians, const glm::vec3& axis) {
        if (node >= 0) {
            locals[node] = locals[node] * glm::rotate(glm::mat4(1.0f), radians, axis);
        }
    };
    rotateLocal(m_hammer, glm::radians(m_rig.hammerCockedDeg * mechanism.hammer), kPartAxisX);
    rotateLocal(m_trigger, glm::radians(m_rig.triggerPulledDeg * mechanism.trigger), kPartAxisX);
    rotateLocal(m_cylinder, m_rig.cylinderDirection * mechanism.cylinder * kChamberStep, kCylinderAxis);
    rotateLocal(m_crane, glm::radians(m_rig.craneOpenDeg * mechanism.crane), kCylinderAxis);
    if (m_ejectRod >= 0) {
        locals[m_ejectRod] = locals[m_ejectRod] *
                             glm::translate(glm::mat4(1.0f), kCylinderAxis * (m_rig.ejectorStrokeMm * 0.001f *
                                                                              mechanism.ejector));
    }

    for (std::size_t i = 0; i < locals.size(); ++i) {
        const PartPose& pose = m_poses[i];
        locals[i] = locals[i] * glm::translate(glm::mat4(1.0f), pose.offsetMm * 0.001f) *
                    glm::mat4_cast(glm::quat(glm::radians(pose.rotationDeg)));
    }
    return locals;
}

std::vector<glm::mat4> RevolverView::worldPose(const glm::mat4& root, const MechanismView& mechanism) const {
    std::vector<glm::mat4> world = m_revolver.computeWorld(posedLocals(mechanism));
    for (glm::mat4& m : world) {
        m = root * m;
    }
    return world;
}

glm::mat4 RevolverView::chamberMatrix(const std::vector<glm::mat4>& world, float chamber, float pushOutMm) const {
    const float angle = -m_rig.cylinderDirection * chamber * kChamberStep;
    return world[m_cylinder] * glm::rotate(glm::mat4(1.0f), angle, kCylinderAxis) *
           glm::translate(glm::mat4(1.0f), kCylinderAxis * (pushOutMm * 0.001f)) * m_roundToCylinder;
}

std::array<glm::mat4, kChamberCount> RevolverView::chamberTransforms(const glm::mat4& root,
                                                                    const MechanismView& mechanism) const {
    std::array<glm::mat4, kChamberCount> result{};
    result.fill(root);
    if (m_cylinder < 0) {
        return result;
    }
    const std::vector<glm::mat4> world = worldPose(root, mechanism);
    for (int k = 0; k < kChamberCount; ++k) {
        result[static_cast<std::size_t>(k)] =
            chamberMatrix(world, static_cast<float>(k), m_rig.ejectorStrokeMm * mechanism.ejector);
    }
    return result;
}

const TexturedMaterial* RevolverView::surfaceFor(std::string_view part) const {
    if (part == "Grip") {
        return &m_walnut;
    }
    if (part == "Casing") {
        return &m_brass;
    }
    if (part == "Bullet") {
        return &m_lead;
    }
    return &m_steel;
}

void RevolverView::drawRound(const engine::Shader& shader, const glm::mat4& transform, ChamberState state,
                             ElementId element) const {
    const ElementDef* def = (m_elements && element < m_elements->size()) ? &(*m_elements)[element] : nullptr;
    const glm::vec3 tint = def ? def->tint : glm::vec3(1.0f);
    const glm::vec3 glow = def ? def->glow : glm::vec3(0.0f);
    for (std::size_t i = 0; i < m_round.nodes.size(); ++i) {
        const engine::ModelNode& node = m_round.nodes[i];
        const bool isCasing = static_cast<int>(i) == m_casingNode;
        if (node.mesh < 0 || state == ChamberState::Empty || (state == ChamberState::Spent && !isCasing)) {
            continue;
        }
        if (isCasing) {
            drawPart(shader, m_roundMeshes[node.mesh], node.name, transform * m_roundWorld[i], false,
                     glm::mix(glm::vec3(1.0f), tint, 0.3f), glm::vec3(0.0f), &m_brass, m_roundWorld[i]);
        } else {
            drawPart(shader, m_roundMeshes[node.mesh], node.name, transform * m_roundWorld[i], false, tint,
                     glow * 0.35f, &m_lead, m_roundWorld[i]);
        }
    }
}

void RevolverView::render(const engine::Shader& shader, const glm::mat4& root, const MechanismView& mechanism) const {
    const std::vector<glm::mat4> world = worldPose(root, mechanism);

    for (std::size_t i = 0; i < m_revolver.nodes.size(); ++i) {
        const engine::ModelNode& node = m_revolver.nodes[i];
        if (node.mesh >= 0) {
            drawPart(shader, m_revolverMeshes[node.mesh], node.name, world[i], static_cast<int>(i) == m_selected, glm::vec3(1.0f),
                     glm::vec3(0.0f), surfaceFor(node.name), m_bindWorld[i]);
        }
    }

    if (m_cylinder < 0) {
        return;
    }

    const float pushOut = m_rig.ejectorStrokeMm * mechanism.ejector;
    for (int chamber = 0; chamber < kChamberCount; ++chamber) {
        const auto k = static_cast<std::size_t>(chamber);
        drawRound(shader, chamberMatrix(world, static_cast<float>(chamber), pushOut), mechanism.chambers[k],
                  mechanism.elements[k]);
    }
    if (mechanism.loadingChamber >= 0) {
        const float t = std::clamp(mechanism.loadProgress, 0.0f, 1.0f);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t);
        drawRound(shader,
                  chamberMatrix(world, static_cast<float>(mechanism.loadingChamber),
                                -m_rig.loadSlideMm * (1.0f - eased)),
                  ChamberState::Live, mechanism.loadingElement);
    }
    if (mechanism.speedloadProgress >= 0.0f) {
        const float t = std::clamp(mechanism.speedloadProgress, 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        for (int i = 0; i < kChamberCount; ++i) {
            const int element = mechanism.speedloadElements[static_cast<std::size_t>(i)];
            if (element >= 0) {
                drawRound(shader,
                          chamberMatrix(world, static_cast<float>((mechanism.speedloadFirst + i) % kChamberCount),
                                        -m_rig.loadSlideMm * 1.3f * (1.0f - eased)),
                          ChamberState::Live, static_cast<ElementId>(element));
            }
        }
    }
    if (mechanism.highlightSlot) {
        const glm::mat4 model = chamberMatrix(world, mechanism.cylinder + 1.0f, 0.0f) *
                                glm::translate(glm::mat4(1.0f), m_roundBase - glm::vec3(0.0f, 0.0f, 0.0008f));
        shader.set("uBaseColor", glm::vec3(0.0f));
        shader.set("uMetallic", 0.0f);
        shader.set("uRoughness", 1.0f);
        shader.set("uHighlight", 0.0f);
        shader.set("uEmissive", glm::vec3(1.0f, 0.78f, 0.28f) * (1.1f + 0.9f * mechanism.highlightPulse));
        shader.set("uModel", model);
        shader.set("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
        m_slotRing.draw();
        shader.set("uEmissive", glm::vec3(0.0f));
    }
}

void RevolverView::debugUi() {
    ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 350.0f, 10.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({340.0f, 0.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Revolver");

    if (ImGui::CollapsingHeader("Mechanism rig", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Hammer cocked", &m_rig.hammerCockedDeg, -90.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Trigger pulled", &m_rig.triggerPulledDeg, -60.0f, 60.0f, "%.0f deg");
        bool reversed = m_rig.cylinderDirection < 0.0f;
        if (ImGui::Checkbox("Cylinder turns the other way", &reversed)) {
            m_rig.cylinderDirection = reversed ? -1.0f : 1.0f;
        }
        ImGui::SliderFloat("Crane open", &m_rig.craneOpenDeg, -120.0f, 120.0f, "%.0f deg");
        ImGui::SliderFloat("Ejector stroke", &m_rig.ejectorStrokeMm, -40.0f, 40.0f, "%.1f mm");
        ImGui::SliderFloat("Load slide", &m_rig.loadSlideMm, 0.0f, 80.0f, "%.0f mm");
    }

    if (ImGui::CollapsingHeader("Parts (manual pose)")) {
        if (ImGui::Button("Reset all poses")) {
            std::fill(m_poses.begin(), m_poses.end(), PartPose{});
        }
        auto drawTree = [&](auto& self, int index, int depth) -> void {
            const engine::ModelNode& node = m_revolver.nodes[index];
            ImGui::PushID(index);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + depth * 12.0f);
            if (ImGui::Selectable(node.name.c_str(), m_selected == index)) {
                m_selected = (m_selected == index) ? -1 : index;
            }
            ImGui::PopID();
            for (int child : node.children) {
                self(self, child, depth + 1);
            }
        };
        for (int root : m_revolver.roots) {
            drawTree(drawTree, root, 0);
        }

        if (m_selected >= 0) {
            const engine::ModelNode& node = m_revolver.nodes[m_selected];
            PartPose& pose = m_poses[m_selected];
            ImGui::SeparatorText(node.name.c_str());
            ImGui::SliderFloat3("Rotate (deg)", &pose.rotationDeg.x, -180.0f, 180.0f, "%.1f");
            ImGui::DragFloat3("Offset (mm)", &pose.offsetMm.x, 0.1f, -100.0f, 100.0f, "%.1f");
            if (ImGui::Button("Reset part")) {
                pose = PartPose{};
            }
            const glm::vec3 pivot = glm::vec3(m_bindWorld[m_selected][3]) * 1000.0f;
            ImGui::TextDisabled("Pivot (model space): %.1f, %.1f, %.1f mm", pivot.x, pivot.y, pivot.z);
            if (node.mesh >= 0) {
                ImGui::TextDisabled("Triangles: %zu", m_revolver.meshes[node.mesh].indices.size() / 3);
            }
        }
    }

    ImGui::End();
}

}
