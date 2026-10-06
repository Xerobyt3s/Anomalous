#include "game/world/fog_field.h"

#include <doctest/doctest.h>

#include <functional>
#include <optional>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 120.0f;

std::function<std::optional<float>(const glm::vec3&, const glm::vec3&)> world(std::optional<float> wallX = std::nullopt) {
    return [wallX](const glm::vec3& from, const glm::vec3& to) -> std::optional<float> {
        std::optional<float> nearest;
        auto consider = [&](float fraction) {
            if (fraction >= 0.0f && fraction <= 1.0f && (!nearest || fraction < *nearest)) {
                nearest = fraction;
            }
        };
        if (from.y >= 0.0f && to.y < 0.0f) {
            consider(from.y / (from.y - to.y));
        }
        if (wallX && from.x <= *wallX && to.x > *wallX) {
            consider((*wallX - from.x) / (to.x - from.x));
        }
        return nearest;
    };
}

FogField settled() {
    FogField fog;
    fog.beginSync();
    fog.syncCloud(1, {0.0f, 0.0f, 0.0f}, 4.0f, 3.0f, 5.0f, 15.0f);
    fog.setReach(1, measureFogReach({0.0f, 0.0f, 0.0f}, 4.0f, 3.0f, world()), {0.0f, 0.0f, 0.0f});
    fog.endSync();
    return fog;
}

}

TEST_CASE("Fog is thick in the middle of the dome and absent outside it") {
    const FogField fog = settled();
    CHECK(fog.density({0.0f, 1.0f, 0.0f}) == doctest::Approx(1.0f));
    CHECK(fog.density({5.0f, 1.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(fog.density({0.0f, 3.5f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(fog.density({0.0f, -1.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(fog.density({3.2f, 0.5f, 0.0f}) > 0.0f);
    CHECK(fog.density({3.2f, 0.5f, 0.0f}) < 1.0f);
    CHECK(fog.inside({1.0f, 1.0f, 1.0f}));
    CHECK_FALSE(fog.inside({6.0f, 1.0f, 0.0f}));
}

TEST_CASE("A cloud blooms from the burst and thins away at the end of its life") {
    FogCloud cloud{1, {0.0f, 0.0f, 0.0f}, 4.0f, 3.0f, 0.0f, 15.0f};
    const glm::vec3 out{2.5f, 0.5f, 0.0f};
    CHECK(fogCloudDensity(cloud, out) == doctest::Approx(0.0f));
    cloud.age = 0.3f;
    const float early = fogCloudDensity(cloud, out);
    cloud.age = 2.0f;
    const float full = fogCloudDensity(cloud, out);
    CHECK(early < full);
    CHECK(full > 0.5f);
    cloud.age = 13.5f;
    CHECK(fogCloudDensity(cloud, out) == doctest::Approx(full * 0.5f));
    cloud.age = 15.0f;
    CHECK(fogCloudDensity(cloud, out) == doctest::Approx(0.0f));
}

TEST_CASE("A bullet tears a tunnel that closes again") {
    FogField fog = settled();
    REQUIRE(fog.punch({-6.0f, 1.0f, 0.0f}, {6.0f, 1.0f, 0.0f}, 0.45f, 2.5f));
    CHECK(fog.density({0.0f, 1.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(fog.density({0.0f, 1.0f, 1.5f}) == doctest::Approx(1.0f));

    for (int i = 0; i < 150; ++i) {
        fog.tick(kDt);
    }
    CHECK(fog.density({0.0f, 1.0f, 0.3f}) > 0.0f);
    for (int i = 0; i < 160; ++i) {
        fog.tick(kDt);
    }
    CHECK(fog.density({0.0f, 1.0f, 0.0f}) == doctest::Approx(1.0f));
    CHECK(fog.holes().empty());
}

TEST_CASE("Fog hides what is behind it, except along a fresh bullet hole") {
    FogField fog = settled();
    const glm::vec3 a{-6.0f, 1.0f, 0.0f};
    const glm::vec3 b{6.0f, 1.0f, 0.0f};
    CHECK(fog.transmittance(a, b) < 0.02f);
    CHECK(fog.transmittance({-6.0f, 1.0f, 8.0f}, {6.0f, 1.0f, 8.0f}) == doctest::Approx(1.0f));
    fog.punch(a, b, 0.45f, 2.5f);
    CHECK(fog.transmittance(a, b) > 0.9f);
    CHECK(fog.transmittance({-6.0f, 1.0f, 2.0f}, {6.0f, 1.0f, 2.0f}) < 0.05f);
}

TEST_CASE("A blast clears a sphere; clearings away from any cloud are not kept") {
    FogField fog = settled();
    CHECK(fog.blast({1.0f, 1.0f, 0.0f}, 2.0f, 5.0f));
    CHECK(fog.density({1.0f, 1.0f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(fog.density({-2.5f, 0.5f, 0.0f}) > 0.5f);
    CHECK_FALSE(fog.blast({30.0f, 1.0f, 0.0f}, 2.0f, 5.0f));
    CHECK(fog.holes().size() == 1);
}

TEST_CASE("The number of holes is capped") {
    FogField fog = settled();
    for (int i = 0; i < 40; ++i) {
        fog.punch({-6.0f, 1.0f, 0.1f * static_cast<float>(i)}, {6.0f, 1.0f, 0.1f * static_cast<float>(i)}, 0.3f, 2.5f);
        fog.tick(kDt);
    }
    CHECK(fog.holes().size() == FogField::kMaxHoles);
}

TEST_CASE("Lightning through a cloud charges it for a while; lightning past it does not") {
    FogField fog = settled();
    CHECK(fog.electrify({-8.0f, 1.0f, 9.0f}, {8.0f, 1.0f, 9.0f}, 1.2f).empty());
    const auto charged = fog.electrify({-8.0f, 1.0f, 0.0f}, {8.0f, 1.0f, 0.0f}, 1.2f);
    REQUIRE(charged.size() == 1);
    CHECK(charged[0] == 1);
    CHECK(fog.clouds()[0].electrified == doctest::Approx(1.2f));
    CHECK(fog.electrify({-8.0f, 1.0f, 0.0f}, {8.0f, 1.0f, 0.0f}, 1.2f).empty());
    for (int i = 0; i < 150; ++i) {
        fog.tick(kDt);
    }
    CHECK(fog.clouds()[0].electrified == doctest::Approx(0.0f));
}

TEST_CASE("Clouds whose volume is gone are dropped, with their holes") {
    FogField fog = settled();
    fog.punch({-6.0f, 1.0f, 0.0f}, {6.0f, 1.0f, 0.0f}, 0.45f, 2.5f);
    fog.beginSync();
    fog.endSync();
    CHECK(fog.clouds().empty());
    CHECK(fog.holes().empty());
    CHECK(fog.transmittance({-6.0f, 1.0f, 0.0f}, {6.0f, 1.0f, 0.0f}) == doctest::Approx(1.0f));
}

TEST_CASE("The pull of a tornado on a cloud builds while it drags and eases off after") {
    FogField fog = settled();
    for (int i = 0; i < 120; ++i) {
        fog.pull(1, {7.0f, 0.0f, 0.0f}, 1.1f * kDt);
        fog.tick(kDt);
    }
    CHECK(fog.clouds()[0].pull > 0.5f);
    CHECK(fog.clouds()[0].pullPoint.x == doctest::Approx(7.0f));
    CHECK(fog.clouds()[0].pullPhase > 0.0f);
    for (int i = 0; i < 300; ++i) {
        fog.tick(kDt);
    }
    CHECK(fog.clouds()[0].pull == doctest::Approx(0.0f));
}

TEST_CASE("Fog shot halfway up a wall fills down to the floor and out from the wall, but not into either") {
    FogField fog;
    const glm::vec3 burst{-0.1f, 1.5f, 0.0f};
    fog.beginSync();
    fog.syncCloud(1, burst, 4.0f, 3.0f, 5.0f, 15.0f);
    fog.endSync();
    fog.setReach(1, measureFogReach(burst, 4.0f, 3.0f, world(0.0f)), burst);
    CHECK(fog.density({-1.0f, 0.3f, 0.0f}) > 0.5f);
    CHECK(fog.density({-2.0f, 1.5f, 1.0f}) > 0.5f);
    CHECK(fog.density({1.0f, 1.5f, 0.0f}) == doctest::Approx(0.0f));
    CHECK(fog.density({0.5f, 0.5f, 0.5f}) == doctest::Approx(0.0f));
    CHECK(fog.density({-1.0f, -0.5f, 0.0f}) == doctest::Approx(0.0f));

    FogCloud puff;
    puff.center = burst;
    puff.radius = 4.0f;
    puff.height = 3.0f;
    puff.age = 5.0f;
    puff.lifetime = 15.0f;
    CHECK(fogCloudDensity(puff, {1.0f, 1.5f, 0.0f}) > 0.5f);
}

TEST_CASE("The reach between two samples blends toward the nearer surface") {
    FogReach reach = filledReach(10.0f);
    reach[0] = 0.5f;
    CHECK(fogReachToward(reach, {1.0f, 0.0f, 0.0f}) == doctest::Approx(0.5f).epsilon(0.05));
    CHECK(fogReachToward(reach, {0.0f, 1.0f, 0.0f}) == doctest::Approx(10.0f).epsilon(0.05));
    const float between = fogReachToward(reach, glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f)));
    CHECK(between > 0.5f);
    CHECK(between < 5.0f);
}

TEST_CASE("A floor under a cloud cuts it flat instead of carving cones into its sides") {
    FogReach reach = filledReach(10.0f);
    reach[3] = 0.5f;
    CHECK(fogReachToward(reach, glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f))) == doctest::Approx(10.0f).epsilon(0.05));
    CHECK(fogReachToward(reach, glm::normalize(glm::vec3(1.0f, -0.3f, 0.0f))) == doctest::Approx(10.0f).epsilon(0.05));
    CHECK(fogFloorFade(reach, 0.2f) == doctest::Approx(1.0f));
    CHECK(fogFloorFade(reach, 0.6f) == doctest::Approx(0.0f));
    CHECK(fogFloorFade(filledReach(kFogUnlimited), 50.0f) == doctest::Approx(1.0f));

    FogField fog;
    const glm::vec3 low{0.0f, 0.6f, 0.0f};
    fog.beginSync();
    fog.syncCloud(1, low, 4.0f, 3.0f, 5.0f, 15.0f);
    fog.endSync();
    fog.setReach(1, measureFogReach(low, 4.0f, 3.0f, world()), low);
    CHECK(fog.density({2.0f, 0.1f, 0.0f}) > 0.5f);
    CHECK(fog.density({2.0f, 0.1f, 2.0f}) > 0.3f);
    CHECK(fog.density({1.0f, -0.4f, 0.0f}) == doctest::Approx(0.0f));
}
