#include "game/ballistics/ballistics.h"

#include <doctest/doctest.h>

#include <variant>

using namespace ghost::game;
using ghost::engine::RayHit;

namespace {
constexpr float kDt = 1.0f / 120.0f;

RaycastFn noHits() {
    return [](const glm::vec3&, const glm::vec3&) -> std::optional<RayHit> { return std::nullopt; };
}

RaycastFn plane(const glm::vec3& point, const glm::vec3& normal, Surface surface) {
    return [=](const glm::vec3& from, const glm::vec3& to) -> std::optional<RayHit> {
        const float a = glm::dot(from - point, normal);
        const float b = glm::dot(to - point, normal);
        if (a < 0.0f || b > 0.0f) {
            return std::nullopt;
        }
        const float t = a / (a - b);
        RayHit hit;
        hit.fraction = t;
        hit.point = from + (to - from) * t;
        hit.normal = normal;
        hit.userData = static_cast<std::uint64_t>(surface);
        return hit;
    };
}

void run(Ballistics& b, const RaycastFn& raycast, float seconds, EventList& events) {
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        b.tick(kDt, raycast, events);
    }
}

}

TEST_CASE("Bullets drop under gravity and slow down from drag") {
    Ballistics b;
    EventList events;
    b.fire({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{});
    run(b, noHits(), 0.25f, events);

    REQUIRE(b.projectiles().size() == 1);
    const Projectile& p = b.projectiles()[0];
    const float drop = 10.0f - p.position.y;
    CHECK(drop == doctest::Approx(0.5f * 9.81f * 0.25f * 0.25f).epsilon(0.1));
    CHECK(glm::length(p.velocity) < b.tuning().muzzleVelocity);
    CHECK(p.position.z < -80.0f);
}

TEST_CASE("A head-on hit stops the bullet and reports the impact") {
    Ballistics b;
    EventList events;
    b.fire({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{});
    run(b, plane({0.0f, 0.0f, -20.0f}, {0.0f, 0.0f, 1.0f}, Surface::Concrete), 0.5f, events);

    CHECK(b.projectiles().empty());
    REQUIRE(events.size() == 1);
    const auto& impact = std::get<ProjectileImpact>(events[0]);
    CHECK(impact.point.z == doctest::Approx(-20.0f));
    CHECK_FALSE(impact.ricochet);
    CHECK(impact.surface == Surface::Concrete);
}

TEST_CASE("A shallow hit on steel ricochets and loses speed") {
    Ballistics b;
    EventList events;
    const glm::vec3 shallow = glm::normalize(glm::vec3(0.0f, -std::sin(glm::radians(10.0f)), -1.0f));
    b.fire({0.0f, 1.0f, 0.0f}, shallow, Round{});

    const RaycastFn steelFloor = plane({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, Surface::Steel);
    for (int i = 0; i < 120 && events.empty(); ++i) {
        b.tick(kDt, steelFloor, events);
    }

    REQUIRE(events.size() == 1);
    CHECK(std::get<ProjectileImpact>(events[0]).ricochet);
    REQUIRE(b.projectiles().size() == 1);
    CHECK(b.projectiles()[0].velocity.y > 0.0f);
    CHECK(glm::length(b.projectiles()[0].velocity) < b.tuning().muzzleVelocity * 0.8f);
}

TEST_CASE("A steep hit on wood does not ricochet") {
    Ballistics b;
    EventList events;
    const glm::vec3 steep = glm::normalize(glm::vec3(0.0f, -1.0f, -1.0f));
    b.fire({0.0f, 1.0f, 0.0f}, steep, Round{});
    run(b, plane({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, Surface::Wood), 0.2f, events);

    REQUIRE(events.size() == 1);
    CHECK_FALSE(std::get<ProjectileImpact>(events[0]).ricochet);
    CHECK(b.projectiles().empty());
}

TEST_CASE("An ethereal round passes through a wall and fades after about five metres") {
    Ballistics b;
    EventList events;
    ShotProfile haze;
    haze.ethereal = true;
    haze.dragPerMetre = 0.95f;
    haze.gravityScale = 0.1f;
    haze.fadeSpeed = 2.0f;
    b.fire({0.0f, 1.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{}, haze);

    const RaycastFn wall = plane({0.0f, 0.0f, -2.0f}, {0.0f, 0.0f, 1.0f}, Surface::Concrete);
    float lastSpeed = b.tuning().muzzleVelocity;
    const ProjectileFaded* faded = nullptr;
    for (int i = 0; i < 240 && !faded; ++i) {
        b.tick(kDt, wall, events);
        for (const Projectile& p : b.projectiles()) {
            const float speed = glm::length(p.velocity);
            CHECK(speed <= lastSpeed + 0.1f);
            CHECK(p.velocity.z <= 0.0f);
            lastSpeed = speed;
        }
        for (const GameEvent& e : events) {
            CHECK_FALSE(std::holds_alternative<ProjectileImpact>(e));
            if (const auto* f = std::get_if<ProjectileFaded>(&e)) {
                faded = f;
            }
        }
    }
    REQUIRE(faded);
    CHECK(faded->point.z < -4.0f);
    CHECK(faded->point.z > -6.0f);
    CHECK(faded->point.y > 1.3f);
    CHECK(b.projectiles().empty());
}

TEST_CASE("A round that strikes a ghost hangs at the hit for its hold, then is gone") {
    Ballistics b;
    EventList events;
    const RaycastFn ghost = plane({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 1.0f}, Surface::Ghost);
    b.fire({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{});
    while (events.empty()) {
        b.tick(kDt, ghost, events);
    }
    REQUIRE(b.projectiles().size() == 1);
    const std::uint32_t id = std::get<ProjectileImpact>(events[0]).projectile;
    b.hold(id, 0.1f);

    run(b, ghost, 0.08f, events);
    REQUIRE(b.projectiles().size() == 1);
    CHECK(b.projectiles()[0].position.z == doctest::Approx(-5.0f));
    CHECK(events.size() == 1);
    run(b, ghost, 0.05f, events);
    CHECK(b.projectiles().empty());

    events.clear();
    b.fire({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{});
    while (events.empty()) {
        b.tick(kDt, ghost, events);
    }
    b.tick(kDt, ghost, events);
    CHECK(b.projectiles().empty());
}

TEST_CASE("An ethereal round goes through a wall but is stopped by a player in its way") {
    Ballistics b;
    EventList events;
    ShotProfile haze;
    haze.ethereal = true;
    haze.dragPerMetre = 0.3f;
    haze.fadeSpeed = 2.0f;
    b.fire({0.0f, 1.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{}, haze);
    const RaycastFn wall = plane({0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, Surface::Concrete);
    const RaycastFn player = plane({0.0f, 0.0f, -3.0f}, {0.0f, 0.0f, 1.0f}, Surface::Player);
    const ProjectileHitFn bodies = [&](const Projectile&, const glm::vec3& from, const glm::vec3& to) { return player(from, to); };
    const ProjectileImpact* hit = nullptr;
    for (int i = 0; i < 240 && !hit; ++i) {
        b.tick(kDt, wall, events, {}, {}, bodies);
        for (const GameEvent& e : events) {
            if (const auto* impact = std::get_if<ProjectileImpact>(&e)) {
                hit = impact;
            }
        }
    }
    REQUIRE(hit != nullptr);
    CHECK(hit->surface == Surface::Player);
    CHECK(hit->point.z == doctest::Approx(-3.0f).epsilon(0.02));
}
