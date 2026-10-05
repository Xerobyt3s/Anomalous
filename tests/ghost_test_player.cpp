#include "game/player/player.h"
#include "game/world/vortex_field.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

#include <doctest/doctest.h>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 120.0f;

void run(Player& player, const PlayerCommand& command, float seconds) {
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        player.tick(command, kDt);
    }
}

float horizontalSpeed(const PlayerState& s) { return glm::length(glm::vec2(s.velocity.x, s.velocity.z)); }

}

TEST_CASE("Walking forward reaches walk speed along -Z at yaw 0") {
    Player player;
    PlayerCommand forward;
    forward.move = {0.0f, 1.0f};
    run(player, forward, 1.0f);

    CHECK(horizontalSpeed(player.state()) == doctest::Approx(player.tuning().walkSpeed));
    CHECK(player.state().position.z < -1.0f);
}

TEST_CASE("Releasing input brings the player to a stop") {
    Player player;
    PlayerCommand forward;
    forward.move = {0.0f, 1.0f};
    run(player, forward, 1.0f);
    run(player, PlayerCommand{}, 1.0f);
    CHECK(horizontalSpeed(player.state()) == doctest::Approx(0.0f));
}

TEST_CASE("Aiming is slower than walking and cancels sprint") {
    Player player;
    PlayerCommand command;
    command.move = {0.0f, 1.0f};
    command.sprint = true;
    command.aim = true;
    run(player, command, 1.0f);

    CHECK_FALSE(player.state().sprinting);
    CHECK(player.state().aiming);
    CHECK(horizontalSpeed(player.state()) == doctest::Approx(player.tuning().aimSpeed));
}

TEST_CASE("A jump leaves the ground and lands again") {
    Player player;
    PlayerCommand jump;
    jump.jump = true;
    player.tick(jump, kDt);
    CHECK_FALSE(player.state().grounded);
    CHECK(player.state().position.y > 0.0f);

    run(player, PlayerCommand{}, 2.0f);
    CHECK(player.state().grounded);
    CHECK(player.state().position.y == doctest::Approx(0.0f));
}

TEST_CASE("Standing still in wind, the player drifts with it") {
    Player player;
    PlayerEnvironment env;
    env.wind = {2.0f, 0.0f, 0.0f};
    for (int i = 0; i < 120; ++i) {
        player.tick(PlayerCommand{}, kDt, env);
    }
    CHECK(player.state().velocity.x == doctest::Approx(2.0f));
    CHECK(player.state().position.x > 1.0f);
    CHECK(player.state().grounded);
}

TEST_CASE("A 4 m/s headwind beats walking but not sprinting") {
    PlayerEnvironment env;
    env.wind = {0.0f, 0.0f, 4.0f};

    Player walker;
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    for (int i = 0; i < 240; ++i) {
        walker.tick(walk, kDt, env);
    }
    CHECK(walker.state().position.z > 0.0f);

    Player sprinter;
    PlayerCommand sprint = walk;
    sprint.sprint = true;
    for (int i = 0; i < 240; ++i) {
        sprinter.tick(sprint, kDt, env);
    }
    CHECK(sprinter.state().position.z < 0.0f);
}

TEST_CASE("A strong updraft lifts the player off the ground, and they come down when it stops") {
    Player player;
    PlayerEnvironment env;
    env.wind = {0.0f, 6.0f, 0.0f};
    for (int i = 0; i < 120; ++i) {
        player.tick(PlayerCommand{}, kDt, env);
    }
    CHECK_FALSE(player.state().grounded);
    CHECK(player.state().position.y > 1.0f);

    run(player, PlayerCommand{}, 3.0f);
    CHECK(player.state().grounded);
}

TEST_CASE("A player left standing near a tornado is dragged in, lifted, and thrown back out") {
    WindVortex vortex;
    vortex.params.radius = 5.5f;

    Player player(glm::vec3(4.0f, 0.0f, 0.0f));
    float maxHeight = 0.0f;
    float closest = 4.0f;
    bool lifted = false;
    float landedAt = 0.0f;
    for (int i = 0; i < 120 * 6 && landedAt == 0.0f; ++i) {
        const PlayerState& s = player.state();
        PlayerEnvironment env;
        env.wind = airVelocity(vortex, s.position + glm::vec3(0.0f, 0.9f, 0.0f)) * vortex.params.playerCoupling;
        player.tick(PlayerCommand{}, kDt, env);
        const float distance = glm::length(glm::vec2(s.position.x, s.position.z));
        maxHeight = std::max(maxHeight, s.position.y);
        closest = std::min(closest, distance);
        lifted = lifted || s.position.y > 1.0f;
        if (lifted && s.grounded) {
            landedAt = distance;
        }
    }
    CHECK(closest < 1.4f);
    CHECK(maxHeight > 1.5f);
    CHECK(maxHeight < 8.0f);
    CHECK(landedAt > 2.8f);
}

TEST_CASE("Sprinting away from the edge of a tornado's pull escapes it") {
    WindVortex vortex;
    vortex.params.radius = 5.5f;

    Player player(glm::vec3(3.5f, 0.0f, 0.0f));
    for (int i = 0; i < 120 * 3; ++i) {
        const PlayerState& s = player.state();
        PlayerEnvironment env;
        env.wind = airVelocity(vortex, s.position + glm::vec3(0.0f, 0.9f, 0.0f)) * vortex.params.playerCoupling;
        PlayerCommand away;
        away.move = {0.0f, 1.0f};
        away.sprint = true;
        away.yaw = std::atan2(s.position.x, -s.position.z);
        player.tick(away, kDt, env);
    }
    const PlayerState& s = player.state();
    CHECK(glm::length(glm::vec2(s.position.x, s.position.z)) > vortex.params.radius);
}

TEST_CASE("Haste makes the player faster for its duration, then wears off") {
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};

    Player normal;
    run(normal, walk, 2.0f);
    Player hasted;
    hasted.applyHaste(5.0f, 1.5f);
    run(hasted, walk, 2.0f);
    CHECK(glm::length(hasted.state().velocity) == doctest::Approx(glm::length(normal.state().velocity) * 1.5f));
    CHECK(-hasted.state().position.z > -normal.state().position.z * 1.4f);

    run(hasted, walk, 3.5f);
    CHECK(hasted.state().hasteTime == doctest::Approx(0.0f));
    CHECK(glm::length(hasted.state().velocity) == doctest::Approx(glm::length(normal.state().velocity)));
}

TEST_CASE("Shroud hides the player from ghosts for its duration") {
    Player player;
    CHECK_FALSE(player.hiddenFromGhosts());
    player.applyShroud(3.0f);
    run(player, PlayerCommand{}, 2.9f);
    CHECK(player.hiddenFromGhosts());
    run(player, PlayerCommand{}, 0.2f);
    CHECK_FALSE(player.hiddenFromGhosts());
}

TEST_CASE("Teleport moves the player at once; in the air they fall and land") {
    Player player;
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    run(player, walk, 1.0f);
    const float speed = glm::length(player.state().velocity);

    player.teleport({3.0f, 2.5f, -4.0f});
    CHECK(player.state().position.x == doctest::Approx(3.0f));
    CHECK(player.previous().position.x == doctest::Approx(3.0f));
    CHECK_FALSE(player.state().grounded);
    CHECK(glm::length(player.state().velocity) == doctest::Approx(speed));

    run(player, PlayerCommand{}, 2.0f);
    CHECK(player.state().grounded);
    CHECK(player.state().position.y == doctest::Approx(0.0f));

    player.teleport({0.0f, -3.0f, 0.0f});
    CHECK(player.state().position.y == doctest::Approx(0.0f));
    CHECK(player.state().grounded);
}

TEST_CASE("Health comes back by itself after a while without being hurt") {
    Player player;
    CHECK_FALSE(player.hurt(0.6f));
    CHECK(player.state().health == doctest::Approx(0.4f));
    run(player, PlayerCommand{}, 3.0f);
    CHECK(player.state().health == doctest::Approx(0.4f));
    run(player, PlayerCommand{}, 2.5f);
    CHECK(player.state().health > 0.5f);
    run(player, PlayerCommand{}, 6.0f);
    CHECK(player.state().health == doctest::Approx(1.0f));
}

TEST_CASE("Being hurt again holds the regeneration off, and enough damage is the end") {
    Player player;
    player.hurt(0.5f);
    run(player, PlayerCommand{}, 3.5f);
    player.hurt(0.1f);
    run(player, PlayerCommand{}, 3.5f);
    CHECK(player.state().health == doctest::Approx(0.4f));
    CHECK(player.hurt(0.5f));
    CHECK(player.state().health == doctest::Approx(0.0f));

    player.respawn({1.0f, 0.0f, 2.0f});
    CHECK(player.state().health == doctest::Approx(1.0f));
    CHECK(player.state().position.x == doctest::Approx(1.0f));
}

TEST_CASE("Crouching lowers the body and the eyes and slows the walk; letting go stands back up") {
    Player player;
    const PlayerTuning& t = player.tuning();
    PlayerCommand crouch;
    crouch.crouch = true;
    crouch.move = {0.0f, 1.0f};
    crouch.sprint = true;
    run(player, crouch, 1.0f);
    CHECK(player.state().stance == Stance::Crouch);
    CHECK(player.state().height == doctest::Approx(t.crouchHeight));
    CHECK(player.state().eyeHeight == doctest::Approx(t.crouchEyeHeight).epsilon(0.01));
    CHECK(horizontalSpeed(player.state()) == doctest::Approx(t.crouchSpeed));
    CHECK_FALSE(player.state().sprinting);

    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    run(player, walk, 1.0f);
    CHECK(player.state().stance == Stance::Stand);
    CHECK(player.state().height == doctest::Approx(t.height));
    CHECK(player.state().eyeHeight == doctest::Approx(t.eyeHeight).epsilon(0.01));
}

TEST_CASE("With no room overhead the player stays crouched") {
    Player player;
    PlayerEnvironment low;
    low.fits = [](const glm::vec3&, float) { return false; };
    PlayerCommand crouch;
    crouch.crouch = true;
    for (int i = 0; i < 30; ++i) {
        player.tick(crouch, kDt, low);
    }
    for (int i = 0; i < 60; ++i) {
        player.tick(PlayerCommand{}, kDt, low);
    }
    CHECK(player.state().stance == Stance::Crouch);
    run(player, PlayerCommand{}, 0.1f);
    CHECK(player.state().stance == Stance::Stand);
}

TEST_CASE("Crouch pressed at a sprint is a slide: a kick of speed that the ground wears down to a crouch") {
    Player player;
    const PlayerTuning& t = player.tuning();
    PlayerCommand sprint;
    sprint.move = {0.0f, 1.0f};
    sprint.sprint = true;
    run(player, sprint, 1.5f);
    REQUIRE(horizontalSpeed(player.state()) == doctest::Approx(t.sprintSpeed));

    PlayerCommand slide = sprint;
    slide.crouch = true;
    player.tick(slide, kDt);
    CHECK(player.state().stance == Stance::Slide);
    CHECK(horizontalSpeed(player.state()) > t.sprintSpeed * 1.05f);
    CHECK(player.state().height == doctest::Approx(t.crouchHeight));

    float last = horizontalSpeed(player.state());
    const float startZ = player.state().position.z;
    for (int i = 0; i < 60; ++i) {
        player.tick(slide, kDt);
        CHECK(horizontalSpeed(player.state()) <= last + 1e-4f);
        last = horizontalSpeed(player.state());
    }
    CHECK(player.state().stance == Stance::Slide);
    CHECK(startZ - player.state().position.z > 2.0f);
    run(player, slide, 1.0f);
    CHECK(player.state().stance == Stance::Crouch);
    CHECK(horizontalSpeed(player.state()) == doctest::Approx(t.crouchSpeed));
}

TEST_CASE("A slide needs a sprint, and can't be chained straight into another") {
    Player player;
    PlayerCommand walk;
    walk.move = {0.0f, 1.0f};
    run(player, walk, 1.0f);
    PlayerCommand crouchWalk = walk;
    crouchWalk.crouch = true;
    player.tick(crouchWalk, kDt);
    CHECK(player.state().stance == Stance::Crouch);

    Player runner;
    PlayerCommand sprint = walk;
    sprint.sprint = true;
    run(runner, sprint, 1.5f);
    PlayerCommand slide = sprint;
    slide.crouch = true;
    runner.tick(slide, kDt);
    REQUIRE(runner.state().stance == Stance::Slide);
    runner.tick(sprint, kDt);
    CHECK(runner.state().stance == Stance::Stand);
    runner.tick(sprint, kDt);
    runner.tick(slide, kDt);
    CHECK(runner.state().stance == Stance::Crouch);
}

TEST_CASE("Leaning in a slide bends its course but never speeds it up, and a jump out of it keeps the speed") {
    Player player;
    PlayerCommand sprint;
    sprint.move = {0.0f, 1.0f};
    sprint.sprint = true;
    run(player, sprint, 1.5f);
    PlayerCommand slide = sprint;
    slide.crouch = true;
    player.tick(slide, kDt);
    const float speed = horizontalSpeed(player.state());

    PlayerCommand lean = slide;
    lean.move = {1.0f, 0.0f};
    run(player, lean, 0.3f);
    CHECK(player.state().velocity.x > 0.5f);
    CHECK(horizontalSpeed(player.state()) < speed);

    const float before = horizontalSpeed(player.state());
    PlayerCommand jump = slide;
    jump.jump = true;
    player.tick(jump, kDt);
    CHECK_FALSE(player.state().grounded);
    CHECK(player.state().stance != Stance::Slide);
    CHECK(horizontalSpeed(player.state()) > before * 0.95f);
}

TEST_CASE("X drops into a crawl: flat, slow and low; X again stands back up") {
    Player player;
    PlayerCommand x;
    x.crawl = true;
    player.tick(x, kDt);
    CHECK(player.state().stance == Stance::Crawl);
    PlayerCommand forward;
    forward.move = {0.0f, 1.0f};
    forward.sprint = true;
    run(player, forward, 1.5f);
    CHECK(horizontalSpeed(player.state()) == doctest::Approx(player.tuning().crawlSpeed).epsilon(0.02));
    CHECK_FALSE(player.state().sprinting);
    CHECK(player.state().height == doctest::Approx(player.tuning().crawlHeight));
    CHECK(player.state().eyeHeight == doctest::Approx(player.tuning().crawlEyeHeight).epsilon(0.02));
    PlayerCommand jump;
    jump.jump = true;
    player.tick(jump, kDt);
    CHECK(player.state().stance == Stance::Stand);
    CHECK(player.state().grounded);
    player.tick(x, kDt);
    CHECK(player.state().stance == Stance::Crawl);
    player.tick(x, kDt);
    CHECK(player.state().stance == Stance::Stand);
}

TEST_CASE("Getting up from a crawl under something low stops at a crouch, or stays down") {
    Player player;
    PlayerCommand x;
    x.crawl = true;
    player.tick(x, kDt);
    PlayerEnvironment crouchRoom;
    crouchRoom.fits = [](const glm::vec3&, float height) { return height < 1.3f; };
    player.tick(x, kDt, crouchRoom);
    CHECK(player.state().stance == Stance::Crouch);
    player.tick(x, kDt);
    PlayerEnvironment flat;
    flat.fits = [](const glm::vec3&, float height) { return height < 0.7f; };
    player.tick(x, kDt, flat);
    CHECK(player.state().stance == Stance::Crawl);
}

TEST_CASE("Space in the air is a dive: thrown forward, once a jump, landing in a belly slide that slows to a crawl") {
    Player player;
    PlayerCommand jump;
    jump.jump = true;
    player.tick(jump, kDt);
    REQUIRE_FALSE(player.state().grounded);
    run(player, PlayerCommand{}, 0.1f);
    player.tick(jump, kDt);
    CHECK(player.state().stance == Stance::Dive);
    CHECK(-player.state().velocity.z >= player.tuning().diveSpeed - 0.01f);
    CHECK(player.state().velocity.y >= player.tuning().diveLift - 0.1f);
    const glm::vec3 after = player.state().velocity;
    player.tick(jump, kDt);
    CHECK(player.state().velocity.y < after.y);
    CHECK(-player.state().velocity.z == doctest::Approx(-after.z).epsilon(0.01));

    int ticks = 0;
    while (!player.state().grounded && ticks++ < 600) {
        player.tick(PlayerCommand{}, kDt);
    }
    player.tick(PlayerCommand{}, kDt);
    CHECK(player.state().stance == Stance::Crawl);
    CHECK(horizontalSpeed(player.state()) > 4.0f);
    run(player, PlayerCommand{}, 2.0f);
    CHECK(player.state().stance == Stance::Crawl);
    CHECK(horizontalSpeed(player.state()) < player.tuning().crawlSpeed + 0.01f);

    player.tick(jump, kDt);
    CHECK(player.state().stance == Stance::Stand);
    player.tick(jump, kDt);
    CHECK_FALSE(player.state().grounded);
}

TEST_CASE("The downed can't crawl or dive") {
    Player player;
    player.setVitals(0.0f, 0.0f, true);
    PlayerCommand x;
    x.crawl = true;
    player.tick(x, kDt);
    CHECK(player.state().stance != Stance::Crawl);
}

TEST_CASE("H puts the revolver away and draws it again, over a moment each way; holstered you move a little faster") {
    Player player;
    PlayerCommand h;
    h.holster = true;
    player.tick(h, kDt);
    CHECK(player.state().holstered);
    run(player, PlayerCommand{}, player.tuning().holsterTime * 0.5f);
    CHECK(player.state().holster > 0.3f);
    CHECK(player.state().holster < 0.7f);
    run(player, PlayerCommand{}, player.tuning().holsterTime * 0.6f);
    CHECK(player.state().holster == doctest::Approx(1.0f));

    PlayerCommand forward;
    forward.move = {0.0f, 1.0f};
    run(player, forward, 1.0f);
    CHECK(horizontalSpeed(player.state()) == doctest::Approx(player.tuning().walkSpeed * player.tuning().holsteredSpeed).epsilon(0.02));
    PlayerCommand aim = forward;
    aim.aim = true;
    player.tick(aim, kDt);
    CHECK_FALSE(player.state().aiming);

    player.tick(h, kDt);
    CHECK_FALSE(player.state().holstered);
    run(player, PlayerCommand{}, player.tuning().drawTime * 0.5f);
    CHECK(player.state().holster > 0.3f);
    run(player, PlayerCommand{}, player.tuning().drawTime * 0.6f);
    CHECK(player.state().holster == doctest::Approx(0.0f));
}

TEST_CASE("No holstering with the hands busy on the gun, or when down; back from it, the gun is in the hand") {
    Player player;
    PlayerCommand busy;
    busy.holster = true;
    busy.handsBusy = true;
    player.tick(busy, kDt);
    CHECK_FALSE(player.state().holstered);
    player.setVitals(0.0f, 0.0f, true);
    PlayerCommand h;
    h.holster = true;
    player.tick(h, kDt);
    CHECK_FALSE(player.state().holstered);
    player.setVitals(1.0f, 10.0f, false);
    player.tick(h, kDt);
    CHECK(player.state().holstered);
    player.respawn({0.0f, 0.0f, 0.0f});
    CHECK_FALSE(player.state().holstered);
    CHECK(player.state().holster == doctest::Approx(0.0f));
}

TEST_CASE("Other players are solid: walking into one stops at them, and at an angle slides along them") {
    Player player({0.0f, 0.0f, 0.0f});
    PlayerEnvironment world;
    world.others.push_back({{0.0f, 0.0f, -2.0f}, 0.3f, 1.6f});
    PlayerCommand forward;
    forward.move = {0.0f, 1.0f};
    for (int i = 0; i < 240; ++i) {
        player.tick(forward, kDt, world);
    }
    const float reach = player.tuning().radius + 0.3f;
    CHECK(glm::distance(glm::vec2(player.state().position.x, player.state().position.z), glm::vec2(0.0f, -2.0f)) >= reach - 0.01f);
    CHECK(player.state().position.z > -2.0f);
    CHECK(horizontalSpeed(player.state()) < 0.5f);

    Player glancing({-0.3f, 0.0f, 0.0f});
    for (int i = 0; i < 240; ++i) {
        glancing.tick(forward, kDt, world);
    }
    CHECK(glancing.state().position.z < -2.5f);
    CHECK(glancing.state().position.x < -0.3f);
}

TEST_CASE("Players on the same spot are pushed apart; a low body does not block one above it") {
    Player player({1.0f, 0.0f, 1.0f});
    PlayerEnvironment world;
    world.others.push_back({{1.0f, 0.0f, 1.0f}, 0.3f, 1.6f});
    player.tick(PlayerCommand{}, kDt, world);
    CHECK(glm::distance(glm::vec2(player.state().position.x, player.state().position.z), glm::vec2(1.0f, 1.0f)) >= player.tuning().radius + 0.3f - 0.01f);

    Player above({0.0f, 1.0f, 0.0f});
    PlayerEnvironment low;
    low.others.push_back({{0.0f, 0.0f, 0.0f}, 0.3f, 0.65f});
    const glm::vec3 before = above.state().position;
    above.tick(PlayerCommand{}, kDt, low);
    CHECK(above.state().position.x == doctest::Approx(before.x));
    CHECK(above.state().position.z == doctest::Approx(before.z));
}

TEST_CASE("Pushed out from another player next to a wall, the body goes through the world's collision") {
    Player player({0.0f, 0.0f, 0.0f});
    PlayerEnvironment world;
    world.others.push_back({{0.1f, 0.0f, 0.0f}, 0.3f, 1.6f});

    world.move = [](const glm::vec3& feet, const glm::vec3& velocity, float dt) {
        ghost::engine::CapsuleMove moved;
        moved.position = feet + velocity * dt;
        moved.position.x = std::max(moved.position.x, -0.2f);
        moved.grounded = true;
        return moved;
    };
    player.tick(PlayerCommand{}, kDt, world);
    CHECK(player.state().position.x >= -0.2f);
}

TEST_CASE("The dive goes the way you are moving; standing still, the way you face") {
    auto diveWith = [](glm::vec2 move) {
        Player player;
        PlayerCommand jump;
        jump.jump = true;
        jump.move = move;
        player.tick(jump, kDt);
        PlayerCommand drift;
        drift.move = move;
        run(player, drift, 0.1f);
        player.tick(jump, kDt);
        return player.state().velocity;
    };
    const glm::vec3 right = diveWith({1.0f, 0.0f});
    CHECK(right.x >= 6.0f);
    CHECK(std::abs(right.z) < 1.0f);
    const glm::vec3 still = diveWith({0.0f, 0.0f});
    CHECK(-still.z >= 6.0f);
}

TEST_CASE("Lying, turning the view rolls the body over along its length: side, back, other side; crawling on, it turns to follow") {
    Player player;
    PlayerCommand x;
    x.crawl = true;
    player.tick(x, kDt);
    CHECK(player.state().lieYaw == doctest::Approx(0.0f));
    PlayerCommand look;
    look.yaw = glm::half_pi<float>() * 0.9f;
    run(player, look, 0.4f);
    CHECK(player.state().roll == doctest::Approx(glm::half_pi<float>() * 0.9f).epsilon(0.02));
    CHECK(player.state().lieYaw == doctest::Approx(0.0f));
    CHECK_FALSE(player.state().onBack());
    look.yaw = glm::pi<float>() * 0.95f;
    run(player, look, 0.4f);
    CHECK(player.state().onBack());

    float last = player.state().roll;
    float worst = 0.0f;
    for (int i = 0; i < 60; ++i) {
        look.yaw += 0.02f;
        player.tick(look, kDt);
        worst = std::max(worst, std::abs(player.state().roll - last));
        last = player.state().roll;
    }
    CHECK(player.state().roll > glm::pi<float>());
    CHECK(worst < 0.1f);

    PlayerCommand crawlOn;
    crawlOn.yaw = 0.4f;
    crawlOn.move = {0.0f, 1.0f};
    Player follower;
    follower.tick(x, kDt);
    run(follower, crawlOn, 2.0f);
    CHECK(follower.state().lieYaw == doctest::Approx(0.4f).epsilon(0.05));
    CHECK(std::abs(follower.state().roll) < 0.05f);

    player.tick(x, kDt);
    CHECK(player.state().roll == doctest::Approx(0.0f));
}

TEST_CASE("Lying, the body swings round to lead the way it moves: backing up turns you onto your back over a second") {
    Player player;
    PlayerCommand x;
    x.crawl = true;
    player.tick(x, kDt);
    run(player, PlayerCommand{}, 0.2f);
    PlayerCommand back;
    back.move = {0.0f, -1.0f};
    run(player, back, 0.6f);
    const float halfway = std::abs(std::remainder(player.state().lieYaw, glm::two_pi<float>()));
    CHECK(halfway > 0.8f);
    CHECK(halfway < 2.6f);
    run(player, back, 0.9f);
    CHECK(std::abs(std::remainder(player.state().lieYaw - glm::pi<float>(), glm::two_pi<float>())) < 0.1f);
    CHECK(player.state().onBack());

    Player strafer;
    strafer.tick(x, kDt);
    PlayerCommand right;
    right.move = {1.0f, 0.0f};
    run(strafer, right, 1.5f);
    CHECK(std::remainder(strafer.state().lieYaw - glm::half_pi<float>(), glm::two_pi<float>()) == doctest::Approx(0.0f).epsilon(0.1));
    CHECK(std::abs(std::abs(strafer.state().roll) - glm::half_pi<float>()) < 0.15f);
}

TEST_CASE("After looking right round while lying, it is on its belly again, not its back") {
    Player player;
    PlayerCommand x;
    x.crawl = true;
    player.tick(x, kDt);
    PlayerCommand look;
    for (int i = 0; i < 240; ++i) {
        look.yaw = glm::two_pi<float>() * static_cast<float>(i) / 240.0f;
        player.tick(look, kDt);
    }
    look.yaw = glm::two_pi<float>();
    run(player, look, 0.5f);
    CHECK(std::cos(player.state().roll) > 0.95f);
    CHECK_FALSE(player.state().onBack());
}

TEST_CASE("The dive rolls like lying down: backwards onto the back, sideways onto a side, and it lands that way") {
    auto dive = [](glm::vec2 move) {
        Player player;
        PlayerCommand jump;
        jump.jump = true;
        jump.move = move;
        player.tick(jump, kDt);
        PlayerCommand drift;
        drift.move = move;
        run(player, drift, 0.1f);
        player.tick(jump, kDt);
        return player;
    };
    Player back = dive({0.0f, -1.0f});
    CHECK(back.state().stance == Stance::Dive);
    CHECK(back.state().onBack());
    int ticks = 0;
    while (back.state().stance != Stance::Crawl && ticks++ < 600) {
        back.tick(PlayerCommand{}, kDt);
    }
    CHECK(back.state().onBack());
    const Player side = dive({1.0f, 0.0f});
    CHECK(std::abs(std::abs(side.state().roll) - glm::half_pi<float>()) < 0.1f);
    const Player front = dive({0.0f, 1.0f});
    CHECK(std::abs(front.state().roll) < 0.1f);
}
