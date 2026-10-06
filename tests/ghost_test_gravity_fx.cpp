#include "game/player/player_hit.h"
#include "game/world/element_volume.h"
#include "game/world/vortex_field.h"

#include <doctest/doctest.h>

#include <cmath>
#include <optional>
#include <vector>

using namespace ghost::game;

TEST_CASE("A player standing sideways is hit through their body, not where an upright one would be") {
    PlayerBody body;
    body.id = 1;
    body.feet = glm::vec3(0.0f);
    body.up = glm::vec3(1.0f, 0.0f, 0.0f);
    const std::vector<PlayerBody> bodies{body};
    const auto through = raycastPlayers(bodies, {1.0f, 0.0f, -5.0f}, {1.0f, 0.0f, 5.0f});
    REQUIRE(through.has_value());
    CHECK(through->id == 1);
    CHECK(std::abs(through->normal.x) < 0.2f);
    CHECK_FALSE(raycastPlayers(bodies, {0.0f, 1.0f, -5.0f}, {0.0f, 1.0f, 5.0f}).has_value());
    const auto head = raycastPlayers(bodies, {headCenter(body).x, 0.0f, -5.0f}, {headCenter(body).x, 0.0f, 5.0f});
    REQUIRE(head.has_value());
    CHECK(head->head);
}

TEST_CASE("An upside-down player is hit below their feet, and a blast pushes them along their own up") {
    PlayerBody body;
    body.feet = glm::vec3(0.0f, 10.0f, 0.0f);
    body.up = glm::vec3(0.0f, -1.0f, 0.0f);
    const std::vector<PlayerBody> bodies{body};
    CHECK(raycastPlayers(bodies, {-5.0f, 9.0f, 0.0f}, {5.0f, 9.0f, 0.0f}).has_value());
    CHECK_FALSE(raycastPlayers(bodies, {-5.0f, 11.0f, 0.0f}, {5.0f, 11.0f, 0.0f}).has_value());
    const glm::vec3 shove = blastShove(body.feet + glm::vec3(0.0f, 0.5f, 0.0f), body, 3.0f, 5.0f);
    CHECK(shove.y < -1.0f);
}

TEST_CASE("A funnel lifts along its own axis") {
    WindVortex v;
    v.params.radius = 5.5f;
    v.axis = glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 core = airVelocity(v, {2.0f, 0.0f, 0.0f});
    CHECK(core.x > 1.0f);
    CHECK(std::abs(core.y) < 1e-3f);
    const glm::vec3 side = airVelocity(v, {1.0f, 0.0f, 3.0f});
    CHECK(side.z < -1.0f);
    CHECK(glm::length(airVelocity(v, {1.0f, 6.0f, 0.0f})) == doctest::Approx(0.0f));
}

TEST_CASE("A settling volume in a sideways field stands against that field and comes to rest on the wall") {
    ElementVolumes volumes;
    const glm::vec3 up{1.0f, 0.0f, 0.0f};
    volumes.spawn(1, glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 10.0f);
    const auto ground = [](const glm::vec3&, const glm::vec3& along) -> std::optional<float> { return glm::dot(glm::vec3(0.0f), along); };
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 240; ++i) {
        volumes.settle(dt, [](ElementId) { return true; }, ground, [&](const glm::vec3&) { return up; });
        for (const ElementVolume& v : volumes.all()) {
            volumes.adjust(v.id, v.velocity * dt, 0.0f);
        }
    }
    const ElementVolume& v = volumes.all().front();
    CHECK(v.normal.x == doctest::Approx(1.0f));
    CHECK(v.center.x == doctest::Approx(ElementVolumes::kRestHeight).epsilon(0.05));
    CHECK(v.center.y == doctest::Approx(0.0f));
}
