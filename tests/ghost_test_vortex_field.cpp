#include "game/world/vortex_field.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace ghost::game;

namespace {
WindVortex testVortex() {
    WindVortex v;
    v.params.radius = 5.5f;
    return v;
}

}

TEST_CASE("The wind is zero outside the radius and above the top") {
    const WindVortex v = testVortex();
    CHECK(glm::length(airVelocity(v, {6.0f, 1.0f, 0.0f})) == doctest::Approx(0.0f));
    CHECK(glm::length(airVelocity(v, {1.0f, v.params.height * 1.2f, 0.0f})) == doctest::Approx(0.0f));
    CHECK(glm::length(airVelocity(v, {2.5f, 1.0f, 0.0f})) > 1.0f);
}

TEST_CASE("Outside the core the wind pulls inward and swirls the way the funnel turns, without lift") {
    const WindVortex v = testVortex();
    const glm::vec3 wind = airVelocity(v, {3.0f, 1.0f, 0.0f});
    CHECK(wind.x < -1.0f);
    CHECK(wind.z < -1.0f);
    CHECK(wind.y == doctest::Approx(0.0f));
}

TEST_CASE("The pull gets stronger toward the funnel") {
    const WindVortex v = testVortex();
    const float far = glm::length(airVelocity(v, {4.5f, 1.0f, 0.0f}));
    const float mid = glm::length(airVelocity(v, {2.5f, 1.0f, 0.0f}));
    const float near = glm::length(airVelocity(v, {1.5f, 1.0f, 0.0f}));
    CHECK(far < mid);
    CHECK(mid < near);
}

TEST_CASE("Inside the core things are lifted, and nothing blows up on the axis") {
    const WindVortex v = testVortex();
    CHECK(airVelocity(v, {0.5f, 1.0f, 0.0f}).y > 2.0f);
    const glm::vec3 axis = airVelocity(v, {0.0f, 1.0f, 0.0f});
    CHECK_FALSE(std::isnan(axis.x + axis.y + axis.z));
    CHECK(glm::length(glm::vec2(axis.x, axis.z)) == doctest::Approx(0.0f));
    CHECK(axis.y > 0.0f);
}

TEST_CASE("Strength scales the wind; an element without a vortex makes none") {
    WindVortex v = testVortex();
    const glm::vec3 full = airVelocity(v, {2.5f, 1.0f, 0.0f});
    v.strength = 0.5f;
    CHECK(glm::length(airVelocity(v, {2.5f, 1.0f, 0.0f})) == doctest::Approx(glm::length(full) * 0.5f));

    WindVortex none;
    CHECK(glm::length(airVelocity(none, {1.0f, 1.0f, 0.0f})) == doctest::Approx(0.0f));
}

TEST_CASE("A vortex builds after touchdown and dies away at the end") {
    VortexParams p;
    p.rampIn = 1.0f;
    p.rampOut = 2.0f;
    CHECK(vortexStrength(p, 0.0f, 10.0f) == doctest::Approx(0.0f));
    CHECK(vortexStrength(p, 5.0f, 10.0f) == doctest::Approx(1.0f));
    CHECK(vortexStrength(p, 9.0f, 10.0f) == doctest::Approx(0.5f));
    CHECK(vortexStrength(p, 10.0f, 10.0f) == doctest::Approx(0.0f));
}

TEST_CASE("Near the top the air flows outward, throwing things clear") {
    const WindVortex v = testVortex();
    const glm::vec3 high = airVelocity(v, {0.8f, v.params.height * 0.8f, 0.0f});
    CHECK(high.x > 1.0f);
    const glm::vec3 low = airVelocity(v, {0.8f, 0.5f, 0.0f});
    CHECK(low.x < 0.0f);
}
