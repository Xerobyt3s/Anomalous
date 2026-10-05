#include "game/world/arena_map.h"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace ghost::game {
namespace {
constexpr glm::vec3 kConcrete{0.42f, 0.41f, 0.4f};
constexpr glm::vec3 kDark{0.3f, 0.3f, 0.32f};
constexpr glm::vec3 kWood{0.36f, 0.24f, 0.13f};
constexpr glm::vec3 kSteel{0.55f, 0.5f, 0.42f};
constexpr float kBenchTop = 0.9f;

float yawToward(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 d = to - from;
    return std::atan2(d.x, -d.z);
}

ArenaMap buildYard() {
    ArenaMap map;
    map.name = "the Yard";
    map.center = {120.0f, 0.0f, 0.0f};
    map.half = {20.0f, 20.0f};
    const glm::vec3 c = map.center;
    const float h = 20.0f;
    constexpr float kSpawnOut = 16.0f;
    constexpr glm::vec3 kSides[4] = {{-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}};
    for (const float side : {-1.0f, 1.0f}) {
        map.boxes.push_back({c + glm::vec3(side * (h + 0.4f), 2.0f, 0.0f), {0.4f, 2.0f, h + 0.8f}, kConcrete, Surface::Concrete});
        map.boxes.push_back({c + glm::vec3(0.0f, 2.0f, side * (h + 0.4f)), {h + 0.8f, 2.0f, 0.4f}, kConcrete, Surface::Concrete});
    }

    map.boxes.push_back({c + glm::vec3(0.0f, 1.4f, 0.0f), {1.6f, 1.4f, 1.6f}, kDark, Surface::Concrete});
    for (const glm::vec3& side : kSides) {
        const glm::vec3 across{side.z, 0.0f, side.x};

        map.boxes.push_back({c + side * 9.0f + glm::vec3(0.0f, 0.65f, 0.0f),
                             glm::abs(across) * 2.2f + glm::abs(side) * 0.25f + glm::vec3(0.0f, 0.65f, 0.0f), kConcrete, Surface::Concrete});
        for (const float along : {-1.0f, 1.0f}) {
            map.boxes.push_back({c + side * 12.0f + across * (along * 6.0f) + glm::vec3(0.0f, 1.6f, 0.0f), {0.5f, 1.6f, 0.5f}, kDark, Surface::Concrete});
            map.boxes.push_back({c + side * 5.0f + across * (along * 4.5f) + glm::vec3(0.0f, 0.45f, 0.0f), {0.6f, 0.45f, 0.6f}, kWood, Surface::Wood});
        }

        map.boxes.push_back({c + (side + across) * 13.0f + glm::vec3(0.0f, 1.0f, 0.0f),
                             glm::abs(side) * 0.05f + glm::abs(across) * 1.2f + glm::vec3(0.0f, 1.0f, 0.0f), kSteel, Surface::Steel});
        const glm::vec3 spawn = c + side * kSpawnOut;
        map.spawns.push_back({spawn, yawToward(spawn, c)});
        map.benches.push_back(c + side * (kSpawnOut + 1.5f) + across * 2.5f + glm::vec3(0.0f, kBenchTop, 0.0f));
    }
    return map;
}

ArenaMap buildAlleys() {
    ArenaMap map;
    map.name = "Alleys";
    map.center = {120.0f, 0.0f, 90.0f};
    map.half = {28.0f, 22.0f};
    const glm::vec3 c = map.center;
    constexpr float kHigh = 3.5f;
    constexpr float kWall = 0.4f;

    auto box = [&](float x0, float x1, float z0, float z1, float y0, float y1, const glm::vec3& color, Surface surface) {
        map.boxes.push_back({c + glm::vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, (z0 + z1) * 0.5f),
                             {(x1 - x0) * 0.5f, (y1 - y0) * 0.5f, (z1 - z0) * 0.5f}, color, surface});
    };

    auto both = [&](float x0, float x1, float z0, float z1, float y0, float y1, const glm::vec3& color, Surface surface) {
        box(x0, x1, z0, z1, y0, y1, color, surface);
        box(-x1, -x0, z0, z1, y0, y1, color, surface);
    };
    const float hx = map.half.x;
    const float hz = map.half.y;

    box(-hx - kWall, hx + kWall, hz, hz + kWall, 0.0f, kHigh, kConcrete, Surface::Concrete);
    box(-hx - kWall, hx + kWall, -hz - kWall, -hz, 0.0f, kHigh, kConcrete, Surface::Concrete);
    both(-hx - kWall, -hx, -hz, hz, 0.0f, kHigh, kConcrete, Surface::Concrete);

    constexpr float kTunnelN = -18.5f;
    constexpr float kJogN = -15.0f;
    constexpr float kMidS = -2.0f;
    constexpr float kMidN = 2.0f;
    constexpr float kLongS = 15.0f;
    constexpr float kRoom = 20.0f;
    constexpr float kGapOut = 11.5f;
    constexpr float kGapIn = 8.5f;

    both(-kRoom, -kGapOut, kMidN, kLongS, 0.0f, kHigh, kDark, Surface::Concrete);
    box(-kGapIn, kGapIn, kMidN, kLongS, 0.0f, kHigh, kDark, Surface::Concrete);

    both(-kRoom, -kGapOut, kTunnelN, kMidS, 0.0f, kHigh, kDark, Surface::Concrete);
    box(-kGapIn, kGapIn, kJogN, kMidS, 0.0f, kHigh, kDark, Surface::Concrete);

    box(-3.0f, 3.0f, -hz, kTunnelN, 0.0f, kHigh, kConcrete, Surface::Concrete);

    box(-kRoom, kRoom, -hz, kJogN, 2.5f, 2.8f, kConcrete, Surface::Concrete);

    box(-0.2f, 0.2f, kMidS, -0.7f, 0.0f, kHigh, kConcrete, Surface::Concrete);
    box(-0.2f, 0.2f, 0.7f, kMidN, 0.0f, kHigh, kConcrete, Surface::Concrete);
    box(-0.2f, 0.2f, -0.7f, 0.7f, 2.2f, kHigh, kConcrete, Surface::Concrete);
    both(-4.2f, -3.0f, -1.9f, -0.7f, 0.0f, 0.9f, kWood, Surface::Wood);
    both(-16.6f, -15.4f, 0.6f, 1.8f, 0.0f, 0.9f, kWood, Surface::Wood);

    box(-0.6f, 0.6f, 17.6f, 18.8f, 0.0f, kHigh, kDark, Surface::Concrete);
    both(-17.25f, -16.75f, 16.5f, 19.5f, 0.0f, 1.3f, kConcrete, Surface::Concrete);
    both(-9.6f, -8.4f, 15.2f, 16.4f, 0.0f, 0.9f, kWood, Surface::Wood);
    both(-5.6f, -4.4f, 20.3f, 21.5f, 0.0f, 0.9f, kWood, Surface::Wood);
    both(-13.2f, -10.8f, hz - 0.1f, hz, 0.0f, 2.0f, kSteel, Surface::Steel);

    both(-11.3f, -10.3f, 13.2f, 14.2f, 0.0f, 0.9f, kWood, Surface::Wood);

    both(-3.1f, -3.0f, -21.6f, -18.9f, 0.0f, 2.0f, kSteel, Surface::Steel);

    both(-21.6f, -20.4f, -9.0f, -7.8f, 0.0f, 0.9f, kWood, Surface::Wood);

    for (const float z : {8.0f, 9.6f}) {
        for (const float side : {-1.0f, 1.0f}) {
            const glm::vec3 at = c + glm::vec3(side * 24.0f, 0.0f, z);
            map.spawns.push_back({at, yawToward(at, c + glm::vec3(side * 19.0f, 0.0f, 0.0f))});
        }
    }
    map.benches = {c + glm::vec3(-26.6f, kBenchTop, 4.0f), c + glm::vec3(26.6f, kBenchTop, 4.0f)};
    return map;
}

}

const ArenaMap& arenaMap(int index) {
    static const ArenaMap yard = buildYard();
    static const ArenaMap alleys = buildAlleys();
    return index == 1 ? alleys : yard;
}

const ArenaSpawn& arenaSpawn(const ArenaMap& map, std::uint8_t player) { return map.spawns[player % map.spawns.size()]; }

float arenaFacing(const ArenaMap& map, const glm::vec3& position) {
    const ArenaSpawn* nearest = &map.spawns.front();
    for (const ArenaSpawn& spawn : map.spawns) {
        if (glm::distance(spawn.position, position) < glm::distance(nearest->position, position)) {
            nearest = &spawn;
        }
    }
    return nearest->yaw;
}

}
