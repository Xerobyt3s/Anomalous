#include "engine/assets/asset_path.h"
#include "game/world/reveal.h"

#include <doctest/doctest.h>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 120.0f;

const AmmoData& ammo() {
    static const AmmoData d = loadAmmoData(ghost::engine::assetPath("data"));
    return d;
}
const GhostData& data() {
    static const GhostData d = loadGhostData(ghost::engine::assetPath("data"), ammo());
    return d;
}

struct Scene {
    GhostWorld world{data()};
    std::vector<RevealPulse> pulses;
    std::vector<RevealMark> marks;
    EventList events;

    void cast() { pulses.push_back({glm::vec3(0.0f), 0.0f, 18.0f, 30.0f, 2.0f, {}}); }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            tickReveal(pulses, marks, world, kDt, events);
        }
    }
};

}

TEST_CASE("A pulse marks a ghost when its edge reaches it, not before") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -18.0f});
    s.cast();
    s.run(0.9f);
    CHECK(s.marks.empty());
    s.run(0.2f);
    REQUIRE(s.marks.size() == 1);
    CHECK(s.marks[0].position.z == doctest::Approx(-18.0f));
    CHECK(s.marks[0].position.y == doctest::Approx(1.5f));
}

TEST_CASE("A ghost beyond the pulse's range is never marked, and the pulse ends there") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -40.0f});
    s.cast();
    s.run(3.0f);
    CHECK(s.marks.empty());
    CHECK(s.pulses.empty());
}

TEST_CASE("A mark is a snapshot: it stays put, is made once per pulse, and expires after its duration") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -6.0f});
    s.cast();
    s.run(0.5f);
    REQUIRE(s.marks.size() == 1);
    const glm::vec3 where = s.marks[0].position;

    s.world.push({0.0f, 1.5f, -5.0f}, 6.0f, 20.0f);
    GhostQuarry nobody;
    nobody.hidden = true;
    GhostContext context;
    context.players = std::span<const GhostQuarry>(&nobody, 1);
    EventList ghostEvents;
    for (int i = 0; i < 60; ++i) {
        s.world.tick(kDt, context, ghostEvents);
    }
    CHECK(glm::distance(s.world.ghosts().front().position, where) > 0.5f);
    s.run(1.0f);
    REQUIRE(s.marks.size() == 1);
    CHECK(glm::distance(s.marks[0].position, where) == doctest::Approx(0.0f));

    s.run(1.0f);
    CHECK(s.marks.empty());
}
