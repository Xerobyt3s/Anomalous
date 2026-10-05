#include "game/fx/lightning.h"

#include <doctest/doctest.h>

#include <algorithm>

using namespace ghost::game;

namespace {
const glm::vec3 kFrom{0.0f, 9.0f, 0.0f};
const glm::vec3 kTo{0.0f, 0.0f, 0.0f};

}

TEST_CASE("The main channel runs exactly from start to end") {
    const Bolt bolt = buildBolt(kFrom, kTo, 7);
    REQUIRE_FALSE(bolt.paths.empty());
    const BoltPath& main = bolt.paths[0];
    CHECK_FALSE(main.branch);
    CHECK(main.points.size() == 65);
    CHECK(glm::distance(main.points.front(), kFrom) == doctest::Approx(0.0f));
    CHECK(glm::distance(main.points.back(), kTo) == doctest::Approx(0.0f));
    CHECK(main.reach.front() == doctest::Approx(0.0f));
    CHECK(main.reach.back() == doctest::Approx(1.0f));
}

TEST_CASE("A bolt is the same for the same seed and different for another") {
    const Bolt a = buildBolt(kFrom, kTo, 42);
    const Bolt b = buildBolt(kFrom, kTo, 42);
    const Bolt c = buildBolt(kFrom, kTo, 43);
    REQUIRE(a.paths[0].points.size() == b.paths[0].points.size());
    float same = 0.0f;
    float different = 0.0f;
    for (std::size_t i = 0; i < a.paths[0].points.size(); ++i) {
        same += glm::distance(a.paths[0].points[i], b.paths[0].points[i]);
        different += glm::distance(a.paths[0].points[i], c.paths[0].points[i]);
    }
    CHECK(same == doctest::Approx(0.0f));
    CHECK(different > 0.5f);
}

TEST_CASE("The channel is jagged but stays near the straight line") {
    const Bolt bolt = buildBolt(kFrom, kTo, 3);
    float farthest = 0.0f;
    for (const glm::vec3& p : bolt.paths[0].points) {
        farthest = std::max(farthest, glm::length(glm::vec2(p.x, p.z)));
    }
    CHECK(farthest > 0.1f);
    CHECK(farthest < 3.0f);
}

TEST_CASE("Branches start on the main channel and taper to a point") {
    BoltParams params;
    params.branches = 5;
    const Bolt bolt = buildBolt(kFrom, kTo, 11, params);
    REQUIRE(bolt.paths.size() >= 6);
    const BoltPath& main = bolt.paths[0];
    int rootedOnMain = 0;
    for (std::size_t p = 1; p < bolt.paths.size(); ++p) {
        const BoltPath& branch = bolt.paths[p];
        CHECK(branch.branch);
        CHECK(branch.width.back() == doctest::Approx(0.0f));
        CHECK(branch.width.front() < 1.0f);
        const bool onMain = std::any_of(main.points.begin(), main.points.end(), [&](const glm::vec3& m) {
            return glm::distance(m, branch.points.front()) < 1e-4f;
        });
        rootedOnMain += onMain ? 1 : 0;
    }
    CHECK(rootedOnMain >= 5);
}

TEST_CASE("No branches when asked for none") {
    BoltParams params;
    params.branches = 0;
    CHECK(buildBolt(kFrom, kTo, 5, params).paths.size() == 1);
}

TEST_CASE("A bolt with a straight start leaves along the line") {
    BoltParams params;
    params.straightStart = 0.35f;
    const Bolt bolt = buildBolt(kFrom, kTo, 9, params);
    const BoltPath& main = bolt.paths[0];
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(glm::length(glm::vec2(main.points[i].x, main.points[i].z)) < 0.05f);
    }
}
