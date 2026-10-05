#include "engine/physics/physics_world.h"
#include "game/player/player.h"

#include <doctest/doctest.h>

using namespace ghost::game;
using ghost::engine::PhysicsWorld;

namespace {
constexpr float kDt = 1.0f / 120.0f;

struct Room {
    PhysicsWorld physics;
    Room() {
        physics.addStaticBox({0.0f, -0.5f, 0.0f}, {50.0f, 0.5f, 50.0f}, 0);
        physics.addStaticBox({0.0f, 1.5f, -3.5f}, {5.0f, 1.5f, 0.5f}, 0);
        physics.addStaticBox({3.0f, 0.25f, 0.0f}, {0.5f, 0.25f, 0.5f}, 0);
        physics.optimize();
    }
    void run(Player& player, const PlayerCommand& command, float seconds) {
        PlayerEnvironment env;
        env.move = [&](const glm::vec3& feet, const glm::vec3& velocity, float dt) {
            return physics.moveCapsule(feet, velocity, dt, player.tuning().radius, player.tuning().height);
        };
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            player.tick(command, kDt, env);
        }
    }
};

}

TEST_CASE("The player stands on the floor and walks freely where nothing is in the way") {
    Room room;
    Player player(glm::vec3(0.0f, 0.0f, 5.0f));
    room.run(player, PlayerCommand{}, 0.5f);
    CHECK(player.state().grounded);
    CHECK(player.state().position.y == doctest::Approx(0.0f).epsilon(0.01));

    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    room.run(player, walk, 1.0f);
    CHECK(player.state().position.z < 2.5f);
    CHECK(player.state().position.y == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("A wall stops the player") {
    Room room;
    Player player(glm::vec3(0.0f, 0.0f, 0.0f));
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    walk.sprint = true;
    room.run(player, walk, 3.0f);

    CHECK(player.state().position.z == doctest::Approx(-3.0f + player.tuning().radius).epsilon(0.02));
    CHECK(std::abs(player.state().velocity.z) < 0.1f);
}

TEST_CASE("Walking into a wall at an angle slides along it") {
    Room room;
    Player player(glm::vec3(0.0f, 0.0f, -2.0f));
    PlayerCommand walk;
    walk.move = {1.0f, 1.0f};
    room.run(player, walk, 1.5f);
    CHECK(player.state().position.z > -3.0f + player.tuning().radius - 0.05f);
    CHECK(player.state().position.x > 1.5f);
}

TEST_CASE("The player can jump onto a low box, stand on it, and falls when walking off") {
    Room room;
    Player player(glm::vec3(1.9f, 0.0f, 0.0f));
    PlayerCommand toBox;
    toBox.yaw = 1.5707963f;
    toBox.move = {0.0f, 1.0f};

    room.run(player, toBox, 1.0f);
    CHECK(player.state().position.x == doctest::Approx(2.5f - player.tuning().radius).epsilon(0.03));

    PlayerCommand jump = toBox;
    jump.jump = true;
    room.run(player, jump, kDt * 1.5f);
    room.run(player, toBox, 0.45f);
    room.run(player, PlayerCommand{}, 0.5f);
    CHECK(player.state().grounded);
    CHECK(player.state().position.y == doctest::Approx(0.5f).epsilon(0.03));

    room.run(player, toBox, 1.2f);
    CHECK(player.state().position.x > 3.8f);
    CHECK(player.state().grounded);
    CHECK(player.state().position.y == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("Walking into a loose box pushes it along") {
    Room room;
    const auto box = room.physics.addDynamicBox({0.0f, 0.25f, 3.0f}, glm::vec3(0.25f), 6.0f, 0);
    Player player(glm::vec3(0.0f, 0.0f, 4.5f));
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    PlayerEnvironment env;
    env.move = [&](const glm::vec3& feet, const glm::vec3& velocity, float dt) {
        return room.physics.moveCapsule(feet, velocity, dt, player.tuning().radius, player.tuning().height);
    };
    for (int i = 0; i < 240; ++i) {
        room.physics.step(kDt);
        player.tick(walk, kDt, env);
    }
    glm::vec3 position;
    glm::quat rotation;
    room.physics.pose(box, position, rotation);
    CHECK(position.z < 2.5f);
    CHECK(player.state().position.z > position.z + 0.4f);
}

TEST_CASE("A waist-high table blocks the player too") {
    PhysicsWorld physics;
    physics.addStaticBox({0.0f, -0.5f, 0.0f}, {200.0f, 0.5f, 200.0f}, 0);
    physics.addStaticBox({0.0f, 0.47f, 0.0f}, {0.45f, 0.47f, 0.3f}, 0);
    physics.optimize();
    Player player(glm::vec3(0.0f, 0.0f, 2.5f));
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    PlayerEnvironment env;
    env.move = [&](const glm::vec3& feet, const glm::vec3& velocity, float dt) {
        return physics.moveCapsule(feet, velocity, dt, player.tuning().radius, player.tuning().height);
    };
    for (int i = 0; i < 300; ++i) {
        player.tick(walk, kDt, env);
    }
    CHECK(player.state().position.z == doctest::Approx(0.6f).epsilon(0.03));
    CHECK(player.state().position.y == doctest::Approx(0.0f).epsilon(0.01));
}

TEST_CASE("A crouched player fits under a beam a standing one walks into, and can't stand up beneath it") {
    PhysicsWorld physics;
    physics.addStaticBox({0.0f, -0.5f, 0.0f}, {50.0f, 0.5f, 50.0f}, 0);
    physics.addStaticBox({0.0f, 1.7f, -3.0f}, {5.0f, 0.3f, 1.0f}, 0);
    physics.optimize();
    auto run = [&](Player& player, const PlayerCommand& command, float seconds) {
        PlayerEnvironment env;
        env.move = [&](const glm::vec3& feet, const glm::vec3& velocity, float dt) {
            return physics.moveCapsule(feet, velocity, dt, player.tuning().radius, player.state().height);
        };
        env.fits = [&](const glm::vec3& feet, float height) {
            return !physics.raycast(feet + glm::vec3(0.0f, 0.5f, 0.0f), feet + glm::vec3(0.0f, height, 0.0f)).has_value();
        };
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            player.tick(command, kDt, env);
        }
    };

    Player standing(glm::vec3(0.0f, 0.0f, 0.0f));
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    run(standing, walk, 2.0f);
    CHECK(standing.state().position.z > -2.0f);

    Player crouched(glm::vec3(0.0f, 0.0f, 0.0f));
    PlayerCommand creep = walk;
    creep.crouch = true;
    run(crouched, creep, 1.9f);
    CHECK(crouched.state().position.z < -2.5f);
    CHECK(crouched.state().position.z > -3.8f);
    run(crouched, PlayerCommand{}, 0.5f);
    CHECK(crouched.state().stance == Stance::Crouch);
    run(crouched, walk, 1.5f);
    CHECK(crouched.state().position.z < -4.4f);
    CHECK(crouched.state().stance == Stance::Stand);
}
