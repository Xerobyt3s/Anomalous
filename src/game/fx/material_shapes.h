#pragma once

#include "game/fx/strands.h"

#include <glm/glm.hpp>

#include <vector>

namespace ghost::game {
constexpr int kWindRibbons = 4;
std::vector<Strands::Point> windRibbon(int index, const glm::vec3& center, float time, float seed, float detach,
                                       const glm::vec3& target);

constexpr int kKnotLoops = 7;
void threadKnot(const glm::vec3& center, float time, float seed, float unspool, const glm::vec3& target,
                std::vector<std::vector<Strands::Point>>& out);

}
