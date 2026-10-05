#include "game/fx/necromite_fx.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr int kPoints = 14;
constexpr float kLength = 0.42f;
constexpr float kGirth = 0.07f;

}

std::vector<Strands::Point> necromiteWorm(const glm::vec3& ground, const glm::vec3& heading, WormPose pose, float progress, float time,
                                          float seed, const glm::vec3& into) {
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 ahead{heading.x, 0.0f, heading.z};
    ahead = glm::length(ahead) > 1e-4f ? glm::normalize(ahead) : glm::vec3(0.0f, 0.0f, -1.0f);
    const glm::vec3 side = glm::cross(ahead, up);
    const float t = glm::clamp(progress, 0.0f, 1.0f);

    const float out = pose == WormPose::Emerge ? t : (pose == WormPose::Crawl ? 1.0f : 1.0f - t);
    std::vector<Strands::Point> points;
    points.reserve(kPoints);
    for (int k = 0; k < kPoints; ++k) {
        const float along = static_cast<float>(k) / static_cast<float>(kPoints - 1);
        const float s = along * kLength;
        glm::vec3 at;
        if (pose == WormPose::Crawl) {
            const float wave = time * 11.0f - along * 7.0f + seed;
            at = ground - ahead * s + side * (0.045f * std::sin(wave)) + up * (kGirth * 0.5f + 0.03f * std::max(0.0f, std::sin(wave * 0.5f + 1.0f)));
        } else if (pose == WormPose::Bore) {
            const glm::vec3 tail = ground - ahead * 0.12f + up * (kGirth * 0.5f);
            const glm::vec3 arch = glm::mix(tail, into, 1.0f - along) + up * (0.1f * std::sin(along * glm::pi<float>()));
            at = arch + side * (0.02f * std::sin(time * 23.0f + along * 9.0f + seed));
        } else {
            const float height = (1.0f - along) * kLength * out;
            const float sway = 0.05f * height / kLength;
            at = ground + up * height + side * (sway * std::sin(time * 9.0f + along * 4.0f + seed)) + ahead * (sway * std::cos(time * 7.0f + along * 5.0f + seed));
        }
        Strands::Point p;
        p.position = at;

        const float body = std::sin(glm::pi<float>() * (0.12f + 0.8f * along));
        const float ring = 0.85f + 0.15f * std::cos(along * 34.0f);
        const bool hidden = pose != WormPose::Crawl && pose != WormPose::Emerge && pose != WormPose::Burrow && (1.0f - along) < t;
        p.width = hidden ? 0.0f : kGirth * body * ring;
        p.color = glm::mix(glm::vec3(0.74f, 0.55f, 0.5f), glm::vec3(0.45f, 0.15f, 0.15f), std::pow(1.0f - along, 6.0f));
        p.alpha = (pose == WormPose::Emerge || pose == WormPose::Burrow) && out < 0.02f ? 0.0f : 1.0f;
        p.glow = 0.0f;
        points.push_back(p);
    }
    return points;
}

}
