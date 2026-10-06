#pragma once

#include "engine/assets/model.h"
#include "engine/render/mesh.h"
#include "game/render/materials.h"
#include "game/weapons/mechanism_view.h"
#include "game/weapons/revolver_mechanism.h"

#include <glm/glm.hpp>

#include <array>
#include <string_view>
#include <vector>

namespace ghost::engine {
class Shader;
}

namespace ghost::game {
struct PartPose {
    glm::vec3 offsetMm{0.0f};
    glm::vec3 rotationDeg{0.0f};
};

struct RevolverRig {
    float hammerCockedDeg = -38.0f;
    float triggerPulledDeg = 20.0f;
    float cylinderDirection = 1.0f;
    float craneOpenDeg = -80.0f;
    float ejectorStrokeMm = -18.0f;
    float loadSlideMm = 35.0f;
};

class RevolverView {
public:
    RevolverView();

    void render(const engine::Shader& shader, const glm::mat4& root, const MechanismView& mechanism) const;
    void debugUi();

    glm::vec3 rearSight() const { return m_rearSight; }
    glm::vec3 frontSight() const { return m_frontSight; }
    glm::vec3 gripCenter() const { return m_gripCenter; }
    glm::vec3 muzzle() const { return m_muzzle; }
    glm::vec3 cylinderGap() const { return m_cylinderGap; }

    glm::vec3 roundCenter() const { return m_roundCenter; }

    std::array<glm::mat4, kChamberCount> chamberTransforms(const glm::mat4& root, const MechanismView& mechanism) const;

    void drawRound(const engine::Shader& shader, const glm::mat4& transform, ChamberState state,
                   ElementId element = kPlainElement) const;

    void setElements(const ElementTable* elements) { m_elements = elements; }

private:
    std::vector<glm::mat4> posedLocals(const MechanismView& mechanism) const;
    std::vector<glm::mat4> worldPose(const glm::mat4& root, const MechanismView& mechanism) const;
    glm::mat4 chamberMatrix(const std::vector<glm::mat4>& world, float chamber, float pushOutMm) const;
    glm::vec3 meshPoint(const engine::ModelData& model, const std::vector<glm::mat4>& world, const char* node,
                        const glm::vec3& boundsWeight, const glm::vec3& fallback) const;

    engine::ModelData m_revolver;
    engine::ModelData m_round;
    std::vector<engine::Mesh> m_revolverMeshes;
    std::vector<engine::Mesh> m_roundMeshes;
    engine::Mesh m_slotRing;
    glm::vec3 m_roundBase{0.0f};
    std::vector<glm::mat4> m_bindWorld;
    std::vector<glm::mat4> m_roundWorld;
    std::vector<PartPose> m_poses;
    RevolverRig m_rig;
    int m_cylinder = -1;
    int m_hammer = -1;
    int m_trigger = -1;
    int m_crane = -1;
    int m_ejectRod = -1;
    int m_casingNode = -1;
    glm::mat4 m_roundToCylinder{1.0f};
    glm::vec3 m_rearSight{0.0f};
    glm::vec3 m_frontSight{0.0f, 0.0f, 0.1f};
    glm::vec3 m_gripCenter{0.0f};
    glm::vec3 m_muzzle{0.0f, 0.0f, 0.3f};
    glm::vec3 m_roundCenter{0.0f};
    glm::vec3 m_cylinderGap{0.0f, 0.07f, 0.11f};
    int m_selected = -1;
    const ElementTable* m_elements = nullptr;

    TexturedMaterial m_walnut;
    TexturedMaterial m_steel;
    TexturedMaterial m_brass;
    TexturedMaterial m_lead;
    const TexturedMaterial* surfaceFor(std::string_view part) const;
};

}
