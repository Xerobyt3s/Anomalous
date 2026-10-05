#include "engine/assets/asset_path.h"
#include "game/ammo/ammo_data.h"
#include "game/ghosts/ghost_data.h"
#include "game/net/protocol.h"
#include "game/world/arena_map.h"
#include "game/world/reveal.h"

#include <doctest/doctest.h>

#include <cmath>
#include <deque>
#include <variant>
#include <vector>

using namespace ghost::game;

namespace {
constexpr float kBody = 0.3f;
constexpr float kTall = 1.75f;
constexpr float kCell = 0.25f;

bool open(const ArenaMap& map, float x, float z, const std::vector<ArenaBox>& extra = {}) {
    if (std::abs(x - map.center.x) > map.half.x - kBody || std::abs(z - map.center.z) > map.half.y - kBody) {
        return false;
    }
    auto blocks = [&](const ArenaBox& box) {
        const bool overhead = box.center.y - box.half.y >= kTall;
        return !overhead && std::abs(x - box.center.x) < box.half.x + kBody && std::abs(z - box.center.z) < box.half.z + kBody;
    };
    for (const ArenaBox& box : map.boxes) {
        if (blocks(box)) {
            return false;
        }
    }
    for (const ArenaBox& box : extra) {
        if (blocks(box)) {
            return false;
        }
    }
    return true;
}

bool reachable(const ArenaMap& map, const glm::vec3& from, const glm::vec3& to, const std::vector<ArenaBox>& extra = {}) {
    const int w = static_cast<int>(map.half.x * 2.0f / kCell) + 1;
    const int h = static_cast<int>(map.half.y * 2.0f / kCell) + 1;
    auto cellOf = [&](const glm::vec3& p) {
        return glm::ivec2(static_cast<int>(std::lround((p.x - map.center.x + map.half.x) / kCell)),
                          static_cast<int>(std::lround((p.z - map.center.z + map.half.y) / kCell)));
    };
    auto at = [&](const glm::ivec2& cell) {
        return glm::vec2(map.center.x - map.half.x + static_cast<float>(cell.x) * kCell, map.center.z - map.half.y + static_cast<float>(cell.y) * kCell);
    };
    std::vector<char> seen(static_cast<std::size_t>(w * h), 0);
    std::deque<glm::ivec2> queue{cellOf(from)};
    const glm::ivec2 goal = cellOf(to);
    while (!queue.empty()) {
        const glm::ivec2 cell = queue.front();
        queue.pop_front();
        if (cell == goal) {
            return true;
        }
        for (const glm::ivec2 step : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
            const glm::ivec2 next = cell + step;
            if (next.x < 0 || next.y < 0 || next.x >= w || next.y >= h || seen[static_cast<std::size_t>(next.y * w + next.x)]) {
                continue;
            }
            seen[static_cast<std::size_t>(next.y * w + next.x)] = 1;
            const glm::vec2 p = at(next);
            if (open(map, p.x, p.y, extra)) {
                queue.push_back(next);
            }
        }
    }
    return false;
}

bool blockedLine(const ArenaMap& map, const glm::vec3& a, const glm::vec3& b) {
    for (int i = 0; i <= 400; ++i) {
        const glm::vec3 p = glm::mix(a, b, static_cast<float>(i) / 400.0f);
        for (const ArenaBox& box : map.boxes) {
            if (std::abs(p.x - box.center.x) < box.half.x && std::abs(p.y - box.center.y) < box.half.y && std::abs(p.z - box.center.z) < box.half.z) {
                return true;
            }
        }
    }
    return false;
}

ArenaBox shut(const ArenaMap& map, float z0, float z1) {
    return {map.center + glm::vec3(0.0f, 1.75f, (z0 + z1) * 0.5f), {0.3f, 1.75f, (z1 - z0) * 0.5f}, glm::vec3(0.0f), Surface::Concrete};
}

}

TEST_CASE("The arenas: the Yard has a place on each side, Alleys is bigger with a room at each end") {
    const ArenaMap& yard = arenaMap(0);
    const ArenaMap& alleys = arenaMap(1);
    CHECK(&arenaMap(7) == &yard);
    CHECK(alleys.half.x * alleys.half.y > yard.half.x * yard.half.y);
    CHECK(glm::distance(yard.center, alleys.center) > 60.0f);
    CHECK(yard.spawns.size() == 4);
    CHECK(yard.benches.size() == 4);

    REQUIRE(alleys.spawns.size() == 4);
    CHECK(alleys.benches.size() == 2);
    for (std::uint8_t id = 0; id < 4; ++id) {
        const ArenaSpawn& spawn = arenaSpawn(alleys, id);
        const bool westEnd = spawn.position.x < alleys.center.x;
        CHECK(westEnd == (id % 2 == 0));

        const glm::vec3 facing{std::sin(spawn.yaw), 0.0f, -std::cos(spawn.yaw)};
        CHECK(facing.x * (westEnd ? 1.0f : -1.0f) > 0.3f);
        CHECK(arenaFacing(alleys, spawn.position + glm::vec3(0.3f, 0.0f, 0.2f)) == doctest::Approx(spawn.yaw));
    }
    CHECK(glm::distance(arenaSpawn(alleys, 0).position, arenaSpawn(alleys, 2).position) > 2.5f * kBody);
    CHECK(glm::distance(arenaSpawn(alleys, 0).position, arenaSpawn(alleys, 2).position) < 3.0f);
    CHECK(glm::distance(arenaSpawn(alleys, 0).position, arenaSpawn(alleys, 1).position) > 40.0f);
}

TEST_CASE("In every arena each spawn stands on open floor, with a bench of its own that can be walked up to") {
    for (int index = 0; index < kArenaMapCount; ++index) {
        const ArenaMap& map = arenaMap(index);
        for (std::size_t i = 0; i < map.spawns.size(); ++i) {
            const glm::vec3 spawn = map.spawns[i].position;
            CHECK(spawn.y == doctest::Approx(0.0f));
            CHECK(open(map, spawn.x, spawn.z));
            const glm::vec3 bench = map.benches[i % map.benches.size()];
            CHECK(glm::distance(spawn, bench) < 12.0f);

            CHECK(std::abs(bench.x - map.center.x) < map.half.x - 0.6f);
            CHECK(std::abs(bench.z - map.center.z) < map.half.y - 0.3f);
            bool beside = false;
            for (const glm::vec3 offset : {glm::vec3(1.2f, 0.0f, 0.0f), glm::vec3(-1.2f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 0.0f, -1.0f)}) {
                const glm::vec3 stand = bench + offset;
                beside = beside || (open(map, stand.x, stand.z) && reachable(map, spawn, stand));
            }
            CHECK(beside);
        }
    }
}

TEST_CASE("Alleys: the two ends cannot see each other, and are joined by each of the three lanes on its own") {
    const ArenaMap& map = arenaMap(1);
    const glm::vec3 west = arenaSpawn(map, 0).position;
    const glm::vec3 east = arenaSpawn(map, 1).position;
    const glm::vec3 eye{0.0f, 1.6f, 0.0f};
    for (std::uint8_t a : {std::uint8_t{0}, std::uint8_t{2}}) {
        for (std::uint8_t b : {std::uint8_t{1}, std::uint8_t{3}}) {
            CHECK(blockedLine(map, arenaSpawn(map, a).position + eye, arenaSpawn(map, b).position + eye));
        }
    }

    const ArenaBox tunnel = shut(map, -22.0f, -8.0f);
    const ArenaBox mid = shut(map, -3.0f, 3.0f);
    const ArenaBox lng = shut(map, 14.0f, 22.0f);
    CHECK(reachable(map, west, east));
    CHECK(reachable(map, west, east, {mid, lng}));
    CHECK(reachable(map, west, east, {tunnel, lng}));
    CHECK(reachable(map, west, east, {tunnel, mid}));
    CHECK_FALSE(reachable(map, west, east, {tunnel, mid, lng}));

    CHECK(blockedLine(map, map.center + glm::vec3(-19.0f, 1.6f, -20.0f), map.center + glm::vec3(19.0f, 1.6f, -20.0f)));
    CHECK(blockedLine(map, map.center + glm::vec3(-19.0f, 1.6f, 1.5f), map.center + glm::vec3(19.0f, 1.6f, 1.5f)));

    bool roofed = false;
    for (const ArenaBox& box : map.boxes) {
        const float under = box.center.y - box.half.y;
        const glm::vec3 d = glm::abs(map.center + glm::vec3(-15.0f, under, -20.0f) - box.center);
        roofed = roofed || (under >= kTall + 0.3f && under < 3.0f && d.x < box.half.x && d.z < box.half.z);
    }
    CHECK(roofed);
}

TEST_CASE("Alleys is the same from both ends: every box has its mirror") {
    const ArenaMap& map = arenaMap(1);
    for (const ArenaBox& box : map.boxes) {
        bool mirrored = false;
        for (const ArenaBox& other : map.boxes) {
            const glm::vec3 flipped{2.0f * map.center.x - other.center.x, other.center.y, other.center.z};
            mirrored = mirrored || (glm::distance(flipped, box.center) < 1e-3f && glm::distance(other.half, box.half) < 1e-3f);
        }
        CHECK(mirrored);
    }
    CHECK(arenaSpawn(map, 1).position.x - map.center.x == doctest::Approx(-(arenaSpawn(map, 0).position.x - map.center.x)));
    CHECK(arenaSpawn(map, 1).position.z == doctest::Approx(arenaSpawn(map, 0).position.z));
}

TEST_CASE("Which arena is played travels with the match: in the snapshot and with a respawn") {
    net::Snapshot snapshot;
    snapshot.arena = true;
    snapshot.arenaMap = 1;
    const auto bytes = net::encode(snapshot);
    net::Reader reader(bytes);
    CHECK(reader.type() == net::Msg::Snapshot);
    const auto got = net::decodeSnapshot(reader);
    REQUIRE(got.has_value());
    CHECK(got->arena);
    CHECK(got->arenaMap == 1);

    const auto respawn = net::encodeRespawn({1.0f, 0.0f, 2.0f}, true, 1);
    net::Reader back(respawn);
    CHECK(back.type() == net::Msg::Respawn);
    CHECK(back.pod<bool>());
    CHECK(back.pod<std::uint8_t>() == 1);
    CHECK(back.pod<glm::vec3>() == glm::vec3(1.0f, 0.0f, 2.0f));
    CHECK(back.ok());
}

TEST_CASE("In the arena a firelight pulse finds the other players, once each, and never the one who cast it") {
    const AmmoData ammo = loadAmmoData(ghost::engine::assetPath("data"));
    const GhostData data = loadGhostData(ghost::engine::assetPath("data"), ammo);
    GhostWorld world{data};
    std::vector<RevealPulse> pulses{{glm::vec3(0.0f), 0.0f, 18.0f, 30.0f, 2.0f, {}}};
    pulses.back().owner = 0;
    std::vector<RevealMark> marks;
    EventList events;
    const std::vector<RevealTarget> players = {{0, {0.5f, 0.0f, 0.0f}}, {1, {9.0f, 0.0f, 0.0f}}, {2, {0.0f, 0.0f, 40.0f}}};
    int found = 0;
    float foundAt = 0.0f;
    for (int i = 0; i < 360; ++i) {
        const std::size_t before = events.size();
        tickReveal(pulses, marks, world, 1.0f / 120.0f, events, players);
        for (std::size_t e = before; e < events.size(); ++e) {
            if (const auto* seen = std::get_if<PlayerRevealed>(&events[e])) {
                ++found;
                foundAt = static_cast<float>(i) / 120.0f;
                CHECK(seen->player == 1);
                CHECK(seen->position.x == doctest::Approx(9.0f));
            }
        }
    }
    CHECK(found == 1);
    CHECK(foundAt == doctest::Approx(0.5f).epsilon(0.1));

    std::vector<RevealPulse> hunt{{glm::vec3(0.0f), 0.0f, 18.0f, 30.0f, 2.0f, {}}};
    EventList quiet;
    for (int i = 0; i < 240; ++i) {
        tickReveal(hunt, marks, world, 1.0f / 120.0f, quiet);
    }
    CHECK(quiet.empty());
}
