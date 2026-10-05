#include "game/fx/draw_order.h"

#include <doctest/doctest.h>

#include <limits>
#include <vector>

using namespace ghost::game;

namespace {
int sliceOf(const std::vector<DrawWindow>& windows, float distance, int* count = nullptr) {
    int found = -1;
    int n = 0;
    for (std::size_t i = 0; i < windows.size(); ++i) {
        if (windows[i].holds(distance)) {
            found = static_cast<int>(i);
            ++n;
        }
    }
    if (count) {
        *count = n;
    }
    return found;
}

}

TEST_CASE("With nothing big in view the see-through part of a frame is one slice holding everything") {
    const auto windows = drawSlices({});
    REQUIRE(windows.size() == 1);
    CHECK(windows[0].holds(0.0f));
    CHECK(windows[0].holds(3.0f));
    CHECK(windows[0].holds(5000.0f));
    CHECK(DrawWindow{}.holds(0.0f));
    CHECK(DrawWindow{}.holds(1e6f));
}

TEST_CASE("Slices run far to near, and every distance is in exactly one") {
    const auto windows = drawSlices({4.0f, 12.0f, 7.5f});
    REQUIRE(windows.size() == 4);
    for (std::size_t i = 1; i < windows.size(); ++i) {
        CHECK(windows[i].farD < windows[i - 1].farD);
        CHECK(windows[i].farD == doctest::Approx(windows[i - 1].nearD));
    }
    for (const float d : {0.0f, 0.5f, 3.99f, 4.0f, 4.01f, 7.5f, 9.0f, 12.0f, 12.01f, 800.0f}) {
        int count = 0;
        sliceOf(windows, d, &count);
        CHECK(count == 1);
    }
}

TEST_CASE("A big thing is at the far edge of its own slice: what is behind it comes earlier, what is in front shares its slice") {
    const auto windows = drawSlices({10.0f, 20.0f});
    const int fog = sliceOf(windows, 10.0f);
    CHECK(sliceOf(windows, 10.5f) < fog);
    CHECK(sliceOf(windows, 9.5f) == fog);
    CHECK(sliceOf(windows, 20.0f) < fog);
    CHECK(sliceOf(windows, 25.0f) < sliceOf(windows, 20.0f));
    CHECK(sliceOf(windows, 0.1f) == static_cast<int>(windows.size()) - 1);
}

TEST_CASE("Things at the same distance share a slice, bad distances are ignored, and a crowd is folded into a limit") {
    CHECK(drawSlices({5.0f, 5.0f, 5.0002f}).size() == 2);
    CHECK(drawSlices({-3.0f, std::numeric_limits<float>::infinity(), 6.0f}).size() == 2);
    std::vector<float> crowd;
    for (int i = 1; i <= 100; ++i) {
        crowd.push_back(static_cast<float>(i));
    }
    const auto windows = drawSlices(crowd, 8);
    CHECK(windows.size() == 8);
    for (const float d : {0.5f, 1.0f, 3.5f, 50.0f, 99.5f, 100.0f, 150.0f}) {
        int count = 0;
        sliceOf(windows, d, &count);
        CHECK(count == 1);
    }

    CHECK(sliceOf(windows, 1.0f) != sliceOf(windows, 2.0f));
    CHECK(sliceOf(windows, 2.0f) != sliceOf(windows, 3.0f));
}
