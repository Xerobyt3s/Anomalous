#include "game/fx/shard_flock.h"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 60.0f;

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

void settle(ShardFlock& flock, const ShardFlock::Input& in, float seconds = 1.5f) {
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        flock.update(kDt, in);
    }
}

float stepAndMeasure(ShardFlock& flock, const ShardFlock::Input& in) {
    std::vector<glm::vec3> before;
    for (const ShardFlock::Shard& s : flock.shards()) {
        before.push_back(s.position);
    }
    flock.update(kDt, in);
    float furthest = 0.0f;
    for (std::size_t i = 0; i < before.size() && i < flock.shards().size(); ++i) {
        furthest = std::max(furthest, glm::distance(before[i], flock.shards()[i].position));
    }
    return furthest;
}

}

TEST_CASE("A Vasskraka's flock has as many shards as it has health, and the ones it loses fall away") {
    ShardFlock flock;
    ShardFlock::Input in;
    in.center = {0.0f, 2.0f, 0.0f};
    in.form = ShardFlock::Form::Raven;
    settle(flock, in);
    CHECK(flock.shards().size() == static_cast<std::size_t>(ShardFlock::kFull));
    CHECK(flock.debris().empty());
    in.size = 0.5f;
    flock.update(kDt, in);
    CHECK(flock.shards().size() == static_cast<std::size_t>(ShardFlock::kFull / 2));
    CHECK(flock.debris().size() == static_cast<std::size_t>(ShardFlock::kFull / 2));
    const float y = flock.debris().front().position.y;
    settle(flock, in, 1.0f);
    REQUIRE_FALSE(flock.debris().empty());
    CHECK(flock.debris().front().position.y < y);
    settle(flock, in, 1.0f);
    CHECK(flock.debris().empty());
    in.size = 1.5f;
    settle(flock, in);
    CHECK(flock.shards().size() == static_cast<std::size_t>(ShardFlock::kFull * 3 / 2));
}

TEST_CASE("The flock takes its shapes: a cluster against the wall, a raven with its wings out, a tight spiked ball") {
    ShardFlock flock;
    ShardFlock::Input in;
    in.center = {3.0f, 2.0f, 0.0f};
    in.normal = {-1.0f, 0.0f, 0.0f};
    in.form = ShardFlock::Form::Cluster;
    settle(flock, in);
    for (const ShardFlock::Shard& s : flock.shards()) {
        CHECK(finite(s.position));
        CHECK(glm::distance(s.position, in.center) < 0.7f);
        CHECK(s.position.x < in.center.x + 0.2f);
        CHECK(glm::dot(s.axis, in.normal) > 0.2f);
    }

    in.form = ShardFlock::Form::Raven;
    in.velocity = {0.0f, 0.0f, -5.0f};
    float widest = 0.0f;
    for (int i = 0; i < 120; ++i) {
        in.center += in.velocity * kDt;
        flock.update(kDt, in);
        float left = 0.0f;
        float right = 0.0f;
        for (const ShardFlock::Shard& s : flock.shards()) {
            left = std::min(left, s.position.x - in.center.x);
            right = std::max(right, s.position.x - in.center.x);
        }
        widest = std::max(widest, right - left);
    }
    CHECK(widest > 1.1f);
    float ahead = -1e9f;
    float behind = 1e9f;
    for (const ShardFlock::Shard& s : flock.shards()) {
        ahead = std::max(ahead, -(s.position.z - in.center.z));
        behind = std::min(behind, -(s.position.z - in.center.z));
    }
    CHECK(ahead > 0.4f);
    CHECK(behind < -0.4f);

    in.form = ShardFlock::Form::Ball;
    in.velocity = glm::vec3(0.0f);
    settle(flock, in);
    for (const ShardFlock::Shard& s : flock.shards()) {
        const glm::vec3 out = s.position - in.center;
        CHECK(glm::length(out) < 0.45f);
        CHECK(glm::dot(glm::normalize(out), s.axis) > 0.7f);
    }
}

TEST_CASE("Changing shape the flock flows: no shard jumps, even through a scatter, a burst and a merge") {
    ShardFlock flock;
    ShardFlock::Input in;
    in.center = {0.0f, 2.0f, 0.0f};
    in.form = ShardFlock::Form::Cluster;
    settle(flock, in);
    auto run = [&](int frames, float limit) {
        for (int i = 0; i < frames; ++i) {
            in.center += in.velocity * kDt;
            CHECK(stepAndMeasure(flock, in) < limit);
        }
    };
    in.form = ShardFlock::Form::Cloud;
    in.velocity = {0.0f, 1.5f, 3.0f};
    run(40, 0.3f);
    in.form = ShardFlock::Form::Raven;
    in.velocity = {5.0f, 0.0f, 0.0f};
    run(60, 0.3f);
    in.form = ShardFlock::Form::Stoop;
    in.velocity = {9.0f, -9.0f, 0.0f};
    run(20, 0.45f);
    in.form = ShardFlock::Form::Ball;
    in.velocity = glm::vec3(0.0f);
    run(40, 0.3f);
    flock.burst(in.center);
    in.form = ShardFlock::Form::Raven;
    float furthest = 0.0f;
    for (int i = 0; i < 18; ++i) {
        CHECK(stepAndMeasure(flock, in) < 0.3f);
        for (const ShardFlock::Shard& s : flock.shards()) {
            furthest = std::max(furthest, glm::distance(s.position, in.center));
        }
    }
    CHECK(furthest > 1.5f);
    settle(flock, in, 1.5f);
    for (const ShardFlock::Shard& s : flock.shards()) {
        CHECK(glm::distance(s.position, in.center) < 1.6f);
    }

    ShardFlock other;
    ShardFlock::Input there = in;
    there.center = in.center + glm::vec3(0.5f, 0.0f, 0.0f);
    there.size = 0.5f;
    settle(other, there);
    flock.takeIn(other);
    in.size = 1.5f;
    run(60, 0.3f);
    CHECK(flock.shards().size() == static_cast<std::size_t>(ShardFlock::countFor(1.5f)));

    flock.shatter();
    CHECK(flock.shards().empty());
    CHECK_FALSE(flock.debris().empty());
}

namespace {
struct Extent {
    float width = 0.0f;
    float lift = 0.0f;
};

Extent extentOf(const ShardFlock& flock, const glm::vec3& center) {
    float left = 0.0f;
    float right = 0.0f;
    float lift = 0.0f;
    for (const ShardFlock::Shard& s : flock.shards()) {
        left = std::min(left, s.position.x - center.x);
        right = std::max(right, s.position.x - center.x);
        lift += (s.position.y - center.y) / static_cast<float>(flock.shards().size());
    }
    return {right - left, lift};
}

}

TEST_CASE("The bird is compact, folds to an arrowhead in a stoop, rears its wings up when it flares; the cloud is wide") {
    ShardFlock flock;
    ShardFlock::Input in;
    in.center = {0.0f, 3.0f, 0.0f};
    in.form = ShardFlock::Form::Raven;
    in.velocity = {0.0f, 0.0f, -5.0f};
    float widest = 0.0f;
    float lowest = 1e9f;
    float highest = -1e9f;
    for (int i = 0; i < 180; ++i) {
        in.center += in.velocity * kDt;
        flock.update(kDt, in);
        if (i > 60) {
            const Extent e = extentOf(flock, in.center);
            widest = std::max(widest, e.width);
            lowest = std::min(lowest, e.lift);
            highest = std::max(highest, e.lift);
        }
    }
    CHECK(widest > 1.1f);
    CHECK(widest < 1.8f);
    CHECK(highest - lowest > 0.02f);
    const float flying = extentOf(flock, in.center).lift;

    in.flare = 1.0f;
    in.velocity = {0.0f, 0.0f, -2.0f};
    float flared = -1e9f;
    for (int i = 0; i < 60; ++i) {
        in.center += in.velocity * kDt;
        flock.update(kDt, in);
        flared = std::max(flared, extentOf(flock, in.center).lift);
    }
    CHECK(flared > std::max(flying, highest) + 0.05f);

    in.flare = 0.0f;
    in.form = ShardFlock::Form::Stoop;
    in.velocity = {0.0f, -6.0f, -9.0f};
    for (int i = 0; i < 90; ++i) {
        in.center += in.velocity * kDt;
        flock.update(kDt, in);
    }
    CHECK(extentOf(flock, in.center).width < 0.75f);

    in.form = ShardFlock::Form::Cloud;
    in.velocity = glm::vec3(0.0f);
    settle(flock, in, 2.0f);
    CHECK(extentOf(flock, in.center).width > 2.2f);
}

TEST_CASE("A change of shape takes its time and runs through the flock: part way, the shards are between the two") {
    ShardFlock flock;
    ShardFlock::Input in;
    in.center = {0.0f, 3.0f, 0.0f};
    in.form = ShardFlock::Form::Cloud;
    settle(flock, in, 2.0f);
    auto meanReach = [&] {
        float sum = 0.0f;
        for (const ShardFlock::Shard& s : flock.shards()) {
            sum += glm::distance(s.position, in.center) / static_cast<float>(flock.shards().size());
        }
        return sum;
    };
    const float cloud = meanReach();
    in.form = ShardFlock::Form::Ball;
    settle(flock, in, 0.5f);
    const float partWay = meanReach();
    CHECK(partWay < cloud * 0.9f);
    CHECK(partWay > 0.4f);
    int still = 0;
    for (const ShardFlock::Shard& s : flock.shards()) {
        still += glm::distance(s.position, in.center) > 0.6f ? 1 : 0;
    }
    CHECK(still > static_cast<int>(flock.shards().size()) / 4);
    settle(flock, in, 1.5f);
    CHECK(extentOf(flock, in.center).width < 0.9f);
}

TEST_CASE("The bird leaves one trail behind it, the cloud several, a roosting cluster none") {
    auto trailsOf = [](const ShardFlock& flock) {
        int n = 0;
        for (const ShardFlock::Trail& trail : flock.trails()) {
            n += trail.points.size() >= 3 ? 1 : 0;
        }
        return n;
    };
    ShardFlock flock;
    ShardFlock::Input in;
    in.center = {0.0f, 3.0f, 0.0f};
    in.form = ShardFlock::Form::Raven;
    in.velocity = {6.0f, 0.0f, 0.0f};
    for (int i = 0; i < 90; ++i) {
        in.center += in.velocity * kDt;
        flock.update(kDt, in);
    }
    CHECK(trailsOf(flock) == 1);
    const ShardFlock::Trail& behind = flock.trails()[0];
    CHECK(behind.points.front().x > behind.points.back().x + 1.0f);
    CHECK(behind.points.front().x < in.center.x);

    in.form = ShardFlock::Form::Cloud;
    in.velocity = glm::vec3(0.0f);
    settle(flock, in, 1.0f);
    CHECK(trailsOf(flock) == ShardFlock::kTrails);

    in.form = ShardFlock::Form::Cluster;
    settle(flock, in, 1.5f);
    CHECK(trailsOf(flock) == 0);
}
