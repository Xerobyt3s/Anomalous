#pragma once

#include "game/ammo/element.h"

#include <map>
#include <optional>
#include <span>
#include <utility>

namespace ghost::game {
class ReactionTable {
public:
    void add(ElementId a, ElementId b, ElementId result);

    std::optional<ElementId> react(ElementId a, ElementId b) const;

    ElementId combine(std::span<const ElementId> elements) const;

private:
    static std::pair<ElementId, ElementId> key(ElementId a, ElementId b) {
        return a < b ? std::pair{a, b} : std::pair{b, a};
    }
    std::map<std::pair<ElementId, ElementId>, ElementId> m_reactions;
};

}
