#include "game/world/reveal.h"

#include <algorithm>

namespace ghost::game {
void tickReveal(std::vector<RevealPulse>& pulses, std::vector<RevealMark>& marks, const GhostWorld& ghosts, float dt,
                EventList& events, std::span<const RevealTarget> players) {
    for (RevealMark& mark : marks) {
        mark.age += dt;
    }
    std::erase_if(marks, [](const RevealMark& m) { return m.age >= m.duration; });

    for (RevealPulse& pulse : pulses) {
        pulse.radius = std::min(pulse.radius + pulse.speed * dt, pulse.range);
        for (const Ghost& ghost : ghosts.ghosts()) {
            const float distance = glm::length(glm::vec2(ghost.position.x - pulse.origin.x, ghost.position.z - pulse.origin.z));
            if (distance > pulse.radius ||
                std::find(pulse.marked.begin(), pulse.marked.end(), ghost.id) != pulse.marked.end()) {
                continue;
            }
            pulse.marked.push_back(ghost.id);
            const float radius = ghosts.def(ghost).radius;
            marks.push_back({ghost.id, ghost.type, ghost.position, radius, 0.0f, pulse.markDuration, ghost.seed});
            events.push_back(GhostRevealed{ghost.id, pulse.markDuration, ghost.position, radius});
        }
        for (const RevealTarget& player : players) {
            const float distance = glm::length(glm::vec2(player.feet.x - pulse.origin.x, player.feet.z - pulse.origin.z));
            if (player.id == pulse.owner || distance > pulse.radius ||
                std::find(pulse.markedPlayers.begin(), pulse.markedPlayers.end(), player.id) != pulse.markedPlayers.end()) {
                continue;
            }
            pulse.markedPlayers.push_back(player.id);
            events.push_back(PlayerRevealed{player.id, pulse.markDuration, player.feet});
        }
    }
    std::erase_if(pulses, [](const RevealPulse& p) { return p.radius >= p.range; });
}

}
