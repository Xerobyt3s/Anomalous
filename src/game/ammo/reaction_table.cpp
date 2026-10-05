#include "game/ammo/reaction_table.h"

namespace ghost::game {
ElementId ElementTable::add(ElementDef def) {
    m_defs.push_back(std::move(def));
    return static_cast<ElementId>(m_defs.size() - 1);
}

std::optional<ElementId> ElementTable::find(std::string_view name) const {
    for (std::size_t i = 0; i < m_defs.size(); ++i) {
        if (m_defs[i].name == name) {
            return static_cast<ElementId>(i);
        }
    }
    return std::nullopt;
}

void ReactionTable::add(ElementId a, ElementId b, ElementId result) { m_reactions[key(a, b)] = result; }

std::optional<ElementId> ReactionTable::react(ElementId a, ElementId b) const {
    if (a == b) {
        return std::nullopt;
    }
    const auto it = m_reactions.find(key(a, b));
    return it == m_reactions.end() ? std::nullopt : std::optional<ElementId>(it->second);
}

ElementId ReactionTable::combine(std::span<const ElementId> elements) const {
    if (elements.empty()) {
        return kPlainElement;
    }
    ElementId result = elements[0];
    for (std::size_t i = 1; i < elements.size(); ++i) {
        if (const auto reacted = react(result, elements[i])) {
            result = *reacted;
        }
    }
    return result;
}

}
