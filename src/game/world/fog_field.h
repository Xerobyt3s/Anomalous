#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace ghost::game {
struct FogParams {
    float height = 0.0f;
    float bulletHole = 0.45f;
    float holeLife = 2.5f;
    float blastHoleLife = 5.0f;
    float electrifyTime = 1.2f;
    float gustHole = 2.0f;
    float windTunnel = 1.7f;
};

inline constexpr std::size_t kFogDirections = 14;
using FogReach = std::array<float, kFogDirections>;
constexpr float kFogUnlimited = 1e4f;


constexpr FogReach filledReach(float value) {
    FogReach r{};
    for (float& v : r) {
        v = value;
    }
    return r;
}

struct FogCloud {
    std::uint32_t id = 0;
    glm::vec3 center{0.0f};
    float radius = 4.0f;
    float height = 3.0f;
    float age = 0.0f;
    float lifetime = 15.0f;
    float electrified = 0.0f;

    glm::vec3 pullPoint{0.0f};
    float pull = 0.0f;
    float pullPhase = 0.0f;
    bool ball = false;
    FogReach reach = filledReach(kFogUnlimited);
    glm::vec3 reachCenter{0.0f};
    bool measured = false;
};

struct FogHole {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float radius = 0.4f;
    float age = 0.0f;
    float life = 2.5f;
};

class FogField {
public:
    static constexpr std::size_t kMaxHoles = 16;
    static constexpr float kGrowTime = 1.0f;
    static constexpr float kFadeTime = 3.0f;

    void beginSync();
    void syncCloud(std::uint32_t id, const glm::vec3& center, float radius, float height, float age, float lifetime);
    void endSync();

    void setReach(std::uint32_t id, const FogReach& reach, const glm::vec3& at);

    float density(const glm::vec3& point) const;

    float transmittance(const glm::vec3& from, const glm::vec3& to) const;
    bool inside(const glm::vec3& point) const { return density(point) > 0.15f; }

    bool punch(const glm::vec3& from, const glm::vec3& to, float radius, float life);
    bool blast(const glm::vec3& center, float radius, float life) { return punch(center, center, radius, life); }

    std::vector<std::uint32_t> electrify(const glm::vec3& from, const glm::vec3& to, float duration);

    void pull(std::uint32_t id, const glm::vec3& point, float amount);

    void tick(float dt);

    const std::vector<FogCloud>& clouds() const { return m_clouds; }
    const std::vector<FogHole>& holes() const { return m_holes; }
    const FogCloud* cloud(std::uint32_t id) const;

private:
    bool touches(const FogCloud& cloud, const glm::vec3& from, const glm::vec3& to, float margin) const;

    std::vector<FogCloud> m_clouds;
    std::vector<FogHole> m_holes;
    std::vector<std::uint32_t> m_seen;
};

bool fogGroundward(std::size_t direction);
float fogFloorFade(const FogReach& reach, float below);
float fogCloudDensity(const FogCloud& cloud, const glm::vec3& point);
float fogHoleClearing(const FogHole& hole, const glm::vec3& point);
float distanceToSegment(const glm::vec3& point, const glm::vec3& from, const glm::vec3& to);

glm::vec3 fogDirection(std::size_t i);

FogReach measureFogReach(const glm::vec3& center, float radius, float height,
                         const std::function<std::optional<float>(const glm::vec3&, const glm::vec3&)>& hit);

float fogReachToward(const FogReach& reach, const glm::vec3& direction);

}
