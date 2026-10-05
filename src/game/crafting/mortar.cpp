#include "game/crafting/mortar.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
Mortar::AddResult Mortar::addDose(MaterialId material) {
    if (static_cast<int>(m_doses.size()) >= m_data->maxDoses) {
        return AddResult::Full;
    }
    if (m_data->materials[material].propellant && hasPropellant()) {
        return AddResult::SecondPropellant;
    }
    if (const auto& element = m_data->materials[material].element) {
        bool hasElement = false;
        for (MaterialId dose : m_doses) {
            hasElement = hasElement || m_data->materials[dose].element.has_value();
        }
        if (hasElement && !m_data->reactions.react(resultElement(), *element)) {
            return AddResult::Fizzled;
        }
    }
    m_doses.push_back(material);
    return AddResult::Added;
}

void Mortar::grind(float radiansSwept) {
    if (m_doses.empty()) {
        return;
    }
    const float needed = m_data->turnsPerDose * static_cast<float>(m_doses.size());
    m_groundTurns = std::min(m_groundTurns + std::abs(radiansSwept) / glm::two_pi<float>(), needed);
}

float Mortar::groundFraction() const {
    if (m_doses.empty()) {
        return 0.0f;
    }

    return std::min(1.0f, m_groundTurns / (m_data->turnsPerDose * static_cast<float>(m_doses.size())));
}

bool Mortar::hasPropellant() const {
    return std::any_of(m_doses.begin(), m_doses.end(),
                       [&](MaterialId m) { return m_data->materials[m].propellant; });
}

ElementId Mortar::resultElement() const {
    std::vector<ElementId> elements;
    for (MaterialId m : m_doses) {
        if (const auto& element = m_data->materials[m].element) {
            elements.push_back(*element);
        }
    }
    return m_data->reactions.combine(elements);
}

std::optional<Round> Mortar::pour() {
    if (!canPour()) {
        return std::nullopt;
    }
    const Round round{resultElement()};
    clear();
    return round;
}

void Mortar::clear() {
    m_doses.clear();
    m_groundTurns = 0.0f;
}

}
