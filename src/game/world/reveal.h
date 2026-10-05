#pragma once

#include "game/events.h"
#include "game/ghosts/ghost_world.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace ghost::game {
struct RevealPulse {
    glm::vec3 origin{0.0f};
    float radius = 0.0f;
    float speed = 18.0f;
    float range = 30.0f;
    float markDuration = 2.0f;
    std::vector<std::uint32_t> marked;
    std::uint8_t owner = 255;
    std::vector<std::uint8_t> markedPlayers;
};

struct RevealMark {
    std::uint32_t ghost = 0;
    GhostTypeId type = 0;
    glm::vec3 position{0.0f};
    float radius = 0.3f;
    float age = 0.0f;
    float duration = 2.0f;
    float seed = 0.0f;
};

struct RevealTarget {
    std::uint8_t id = 0;
    glm::vec3 feet{0.0f};
};

void tickReveal(std::vector<RevealPulse>& pulses, std::vector<RevealMark>& marks, const GhostWorld& ghosts, float dt,
                EventList& events, std::span<const RevealTarget> players = {});

}
