#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace ghost::game {
using PlayerId = std::uint8_t;
inline constexpr PlayerId kNoPlayer = 255;
inline constexpr int kMaxPlayers = 4;

inline constexpr int kZombieIdBase = 16;

struct PlayerBody {
    PlayerId id = 0;
    glm::vec3 feet{0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float radius = 0.3f;
    float height = 1.62f;
    glm::vec3 eye{0.0f};
    glm::vec3 viewDirection{0.0f, 0.0f, -1.0f};
    bool hidden = false;
    float visibility = 1.0f;
    bool downed = false;
    bool zombie = false;
    bool possessed = false;
};

struct PlayerRayHit {
    PlayerId id = 0;
    float fraction = 0.0f;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool head = false;
};

inline constexpr float kHeadRadius = 0.25f;

glm::vec3 headCenter(const PlayerBody& body);
glm::vec3 bodyPoint(const PlayerBody& body, float along);

std::optional<float> segmentCapsule(const glm::vec3& from, const glm::vec3& to, const glm::vec3& feet, float radius,
                                    float height, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f));

std::optional<PlayerRayHit> raycastPlayers(std::span<const PlayerBody> players, const glm::vec3& from, const glm::vec3& to,
                                           PlayerId ignore = kNoPlayer);

glm::vec3 blastShove(const glm::vec3& center, const PlayerBody& body, float radius, float knockback);

}
