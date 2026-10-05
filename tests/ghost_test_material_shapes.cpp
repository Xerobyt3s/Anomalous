#include "game/fx/material_shapes.h"

#include <doctest/doctest.h>

#include <cmath>

using namespace ghost::game;

namespace {
bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}

TEST_CASE("Gale Wisp's ribbons orbit close round the cloud, and once torn off their heads reach whoever took it") {
    const glm::vec3 center{1.0f, 0.5f, -2.0f};
    const glm::vec3 target{0.0f, 1.2f, 0.0f};
    for (int i = 0; i < kWindRibbons; ++i) {
        for (const Strands::Point& p : windRibbon(i, center, 3.7f, 1.0f, 0.0f, target)) {
            CHECK(finite(p.position));
            CHECK(glm::distance(p.position, center) < 0.13f);
        }
        const auto gone = windRibbon(i, center, 3.7f, 1.0f, 1.0f, target);
        CHECK(glm::distance(gone.front().position, target) < 0.01f);
    }
}

TEST_CASE("Effigy Thread is a knot of closed loops; unspooled, the thread runs out to whoever took it and all of it goes in") {
    const glm::vec3 center{0.0f, 0.3f, -1.5f};
    const glm::vec3 target{0.0f, 1.2f, 0.0f};
    std::vector<std::vector<Strands::Point>> wound;
    threadKnot(center, 2.0f, 0.5f, 0.0f, center, wound);
    REQUIRE(wound.size() == static_cast<std::size_t>(kKnotLoops));
    for (const auto& loop : wound) {
        CHECK(glm::distance(loop.front().position, loop.back().position) < 1e-4f);
        for (const Strands::Point& p : loop) {
            CHECK(glm::distance(p.position, center) < 0.05f);
        }
    }
    std::vector<std::vector<Strands::Point>> pulling;
    threadKnot(center, 2.0f, 0.5f, 0.4f, target, pulling);
    REQUIRE(pulling.size() == 2);
    CHECK(glm::distance(pulling[0].back().position, target) < 0.01f);
    std::vector<std::vector<Strands::Point>> done;
    threadKnot(center, 2.0f, 0.5f, 1.0f, target, done);
    REQUIRE(done.size() == 1);
    for (const Strands::Point& p : done[0]) {
        CHECK(glm::distance(p.position, target) < 0.01f);
    }
}
