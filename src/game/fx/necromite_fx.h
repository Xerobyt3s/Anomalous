#pragma once

#include "game/fx/strands.h"

#include <glm/glm.hpp>

#include <vector>

namespace ghost::game {
enum class WormPose {
    Emerge,
    Crawl,
    Bore,
    Burrow
};

std::vector<Strands::Point> necromiteWorm(const glm::vec3& ground, const glm::vec3& heading, WormPose pose, float progress, float time,
                                          float seed, const glm::vec3& into = glm::vec3(0.0f));

}
