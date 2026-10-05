#pragma once

#include "game/ammo/reaction_table.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghost::game {
using MaterialId = std::uint8_t;

struct MaterialDef {
    std::string name;
    std::string display;
    bool propellant = false;
    std::optional<ElementId> element;
    glm::vec3 color{0.5f};

    enum class Look : std::uint8_t { Glow, StormOrb, HazeBubble, WindCloud, SmallFire, ThreadKnot };
    Look look = Look::Glow;
};

struct AmmoData {
    ElementTable elements;
    ReactionTable reactions;
    std::vector<MaterialDef> materials;
    int maxDoses = 3;
    float turnsPerDose = 2.5f;

    std::optional<MaterialId> findMaterial(std::string_view name) const;
    ElementId element(std::string_view name) const;
};

AmmoData parseAmmoData(std::string_view elementsJson, std::string_view reactionsJson, std::string_view materialsJson);
AmmoData loadAmmoData(const std::filesystem::path& dataDir);

}
