#include "game/world/breath.h"

#include <doctest/doctest.h>

using namespace ghost::game;

namespace {
Breath burst(float age) {
    Breath b;
    b.age = age;
    return b;
}

}

TEST_CASE("A breath rushes out to its range in the first half of its life") {
    CHECK(breathReach(burst(0.0f)) == doctest::Approx(0.0f));
    CHECK(breathReach(burst(0.1f)) > 2.5f);
    CHECK(breathReach(burst(0.25f)) == doctest::Approx(5.0f));
    CHECK(breathReach(burst(0.4f)) == doctest::Approx(5.0f));
}

TEST_CASE("A breath touches what is in its cone, and nothing behind, beside or beyond it") {
    const Breath b = burst(0.3f);
    CHECK(breathTouches(b, {0.0f, 0.0f, -3.0f}));
    CHECK(breathTouches(b, {0.7f, 0.0f, -3.0f}));
    CHECK_FALSE(breathTouches(b, {1.6f, 0.0f, -3.0f}));
    CHECK_FALSE(breathTouches(b, {0.0f, 0.0f, 1.0f}));
    CHECK_FALSE(breathTouches(b, {0.0f, 0.0f, -6.0f}));
    CHECK(breathTouches(b, {0.1f, 0.0f, -0.05f}));
}

TEST_CASE("A breath has not reached far things yet when it starts, and is over after its duration") {
    CHECK_FALSE(breathTouches(burst(0.02f), {0.0f, 0.0f, -4.0f}));
    CHECK(breathTouches(burst(0.3f), {0.0f, 0.0f, -4.0f}));
    CHECK_FALSE(breathTouches(burst(0.5f), {0.0f, 0.0f, -2.0f}));
}
