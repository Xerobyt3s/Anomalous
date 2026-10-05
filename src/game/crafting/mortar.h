#pragma once

#include "game/ammo/ammo_data.h"
#include "game/ammo/round.h"

#include <optional>
#include <vector>

namespace ghost::game {
class Mortar {
public:
    explicit Mortar(const AmmoData& data) : m_data(&data) {}

    enum class AddResult { Added, Full, SecondPropellant, Fizzled };
    AddResult addDose(MaterialId material);

    void grind(float radiansSwept);

    const std::vector<MaterialId>& doses() const { return m_doses; }
    bool empty() const { return m_doses.empty(); }

    float groundFraction() const;
    bool hasPropellant() const;
    bool canPour() const { return hasPropellant() && groundFraction() >= 1.0f; }

    ElementId resultElement() const;

    std::optional<Round> pour();
    void clear();

private:
    const AmmoData* m_data;
    std::vector<MaterialId> m_doses;
    float m_groundTurns = 0.0f;
};

}
