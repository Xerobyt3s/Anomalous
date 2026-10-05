#pragma once

#include "game/ballistics/surface.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace ghost::game {
struct ArenaBox {
    glm::vec3 center{0.0f};
    glm::vec3 half{0.5f};
    glm::vec3 color{0.4f};
    Surface surface = Surface::Concrete;
};

struct ArenaSpawn {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

struct ArenaMap {
    const char* name = "";
    glm::vec3 center{0.0f};
    glm::vec2 half{20.0f};
    std::vector<ArenaBox> boxes;
    std::vector<ArenaSpawn> spawns;
    std::vector<glm::vec3> benches;
};

inline constexpr int kArenaMapCount = 2;

const ArenaMap& arenaMap(int index);

const ArenaSpawn& arenaSpawn(const ArenaMap& map, std::uint8_t player);

float arenaFacing(const ArenaMap& map, const glm::vec3& position);

}
