#pragma once

#include "game/ghosts/ghost_data.h"
#include "game/ghosts/ghost_world.h"

#include <glm/glm.hpp>

#include <vector>

namespace ghost::game {

struct PoseOption {
    const char* label = "";
    GhostState state = GhostState::Wander;
};

std::vector<PoseOption> posesOf(GhostBehavior behavior);

GhostWorld::Pose specimenPose(const GhostDef& def, int pose, const glm::vec3& at, const glm::vec3& forward, const glm::vec3& up,
                              float size);

}
