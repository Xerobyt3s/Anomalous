#include "engine/core/game_loop.h"

#include <doctest/doctest.h>

using ghost::engine::FixedStepClock;

TEST_CASE("FixedStepClock hands out whole ticks and keeps the remainder") {
    FixedStepClock clock(100.0);
    CHECK(clock.advance(0.025) == 2);
    CHECK(clock.alpha() == doctest::Approx(0.5));
    CHECK(clock.advance(0.005) == 1);
    CHECK(clock.alpha() == doctest::Approx(0.0));
}

TEST_CASE("FixedStepClock clamps long frames") {
    FixedStepClock clock(100.0, 0.1);
    CHECK(clock.advance(5.0) == 10);
}

TEST_CASE("FixedStepClock ignores negative frame time") {
    FixedStepClock clock(100.0);
    CHECK(clock.advance(-1.0) == 0);
}
