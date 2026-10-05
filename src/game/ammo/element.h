#pragma once

#include "game/world/fog_field.h"
#include "game/world/vortex_field.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghost::game {
using ElementId = std::uint8_t;
inline constexpr ElementId kPlainElement = 0;

enum class DamageKind : std::uint8_t { Plain, Fire, Wind, Lightning, Count };

enum class SelfEffect : std::uint8_t { None, Hit, Haste, Shroud, Blink, Reveal };

enum class TrailKind : std::uint8_t { None, Embers, Whirl, FlamingWhirl, Haze, Ribbon };

struct ElementDef {
    std::string name;
    std::string display;
    glm::vec3 tint{1.0f};
    glm::vec3 glow{0.0f};
    float velocityScale = 1.0f;
    float dragScale = 1.0f;
    TrailKind trail = TrailKind::None;
    DamageKind damage = DamageKind::Plain;

    float volumeRadius = 0.0f;
    float volumeLifetime = 0.0f;
    float gustImpulse = 0.0f;

    float blastRadius = 0.0f;
    float blastKnockback = 0.0f;
    float flameCone = 0.0f;
    VortexParams vortex;
    float driftSpeed = 0.0f;
    FogParams fog;

    float breathRange = 0.0f;
    float breathHalfAngle = 0.28f;
    float breathDuration = 0.5f;

    float explosionRadiusScale = 0.0f;
    float explosionImpulse = 0.0f;
    float explosionGhostDamage = 0.0f;
    float explosionPlayerDamage = 0.0f;
    float explosionLifetime = 1.6f;
    float hitscanRange = 0.0f;

    float strikeDelay = 0.0f;
    float strikeCloudHeight = 9.0f;
    float strikeRadius = 3.5f;
    float strikeImpulse = 16.0f;
    float strikePower = 1.0f;

    bool ethereal = false;
    float etherealDrag = 1.1f;
    float gravityScale = 1.0f;
    float fadeSpeed = 0.0f;
    bool stealthy = false;

    SelfEffect self = SelfEffect::None;
    float selfDuration = 0.0f;
    float selfSpeedScale = 1.0f;
    float selfDistance = 0.0f;
    float selfSpeed = 0.0f;
    float selfHitStagger = 0.0f;
};

class ElementTable {
public:
    ElementId add(ElementDef def);

    const ElementDef& operator[](ElementId id) const { return m_defs[id]; }
    std::optional<ElementId> find(std::string_view name) const;
    std::size_t size() const { return m_defs.size(); }

private:
    std::vector<ElementDef> m_defs;
};

}
