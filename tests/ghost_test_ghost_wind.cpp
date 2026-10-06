#include "engine/assets/asset_path.h"
#include "game/ghosts/ghost_world.h"
#include "game/world/vortex_field.h"

#include <doctest/doctest.h>

#include <cmath>
#include <variant>

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

GhostTypeId typeOf(const char* name) { return static_cast<GhostTypeId>(data().type(name)); }

template <typename T>
int count(const EventList& events) {
    int n = 0;
    for (const GameEvent& e : events) {
        n += std::holds_alternative<T>(e) ? 1 : 0;
    }
    return n;
}

struct Funnel {
    GhostWorld world{data()};
    GhostQuarry player{glm::vec3(0.0f, 0.0f, 40.0f), glm::vec3(0.0f, 1.62f, 40.0f), glm::vec3(0.0f, 0.0f, -1.0f), false};
    GhostContext context;
    EventList events;
    WindVortex vortex;
    bool blowing = true;
    float highest = 0.0f;
    float closest = 1e9f;
    float caughtFor = 0.0f;

    Funnel() {
        vortex.params = ammo().elements[ammo().element("inferno")].vortex;
        context.wind = [this](const glm::vec3& at) { return blowing ? airVelocity(vortex, at) : glm::vec3(0.0f); };
    }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            context.players = std::span<const GhostQuarry>(&player, 1);
            world.tick(kDt, context, events);
            if (!world.ghosts().empty()) {
                const Ghost& g = world.ghosts().front();
                highest = std::max(highest, g.position.y);
                closest = std::min(closest, glm::length(glm::vec2(g.position.x, g.position.z)));
                caughtFor += g.caught > 0.0f ? kDt : 0.0f;
            }
        }
    }
    const Ghost& ghost() const { return world.ghosts().front(); }
};

}

TEST_CASE("A tornado catches wisps, poltergeists, vasskrakas and mimics, drags them in and lifts them") {
    for (const char* name : {"wisp", "poltergeist", "vasskraka", "mimic"}) {
        CAPTURE(name);
        Funnel s;
        s.world.spawn(typeOf(name), glm::vec3(3.0f, 0.6f, 0.0f));
        s.run(2.0f);
        REQUIRE_FALSE(s.world.ghosts().empty());
        CHECK(s.caughtFor > 0.5f);
        CHECK(s.closest < 1.8f);
        CHECK(s.highest > 1.5f);
    }
}

TEST_CASE("A mimic caught in a tornado goes limp and tumbles, then lands and grips the ground again") {
    Funnel s;
    s.world.spawn(typeOf("mimic"), glm::vec3(3.0f, 0.6f, 0.0f));
    s.run(0.6f);
    REQUIRE(s.ghost().caught > 0.0f);
    CHECK(count<MimicRevealed>(s.events) == 1);
    CHECK_FALSE(s.ghost().attached);
    const glm::vec3 facing = s.ghost().surfaceNormal;
    s.run(0.3f);
    CHECK(glm::dot(facing, s.ghost().surfaceNormal) < 0.98f);
    s.blowing = false;
    s.run(4.0f);
    CHECK(s.ghost().caught == 0.0f);
    CHECK(s.ghost().attached);
    CHECK(s.ghost().position.y < 1.0f);
}

TEST_CASE("Ball lightning and necromites are not moved by a tornado") {
    for (const char* name : {"ball_lightning", "necromite"}) {
        CAPTURE(name);
        Funnel windy;
        Funnel calm;
        calm.blowing = false;
        windy.world.spawn(typeOf(name), glm::vec3(2.0f, 0.6f, 0.0f));
        calm.world.spawn(typeOf(name), glm::vec3(2.0f, 0.6f, 0.0f));
        windy.run(2.0f);
        calm.run(2.0f);
        REQUIRE(windy.world.ghosts().size() == calm.world.ghosts().size());
        if (!windy.world.ghosts().empty()) {
            CHECK(glm::distance(windy.ghost().position, calm.ghost().position) < 1e-3f);
            CHECK(windy.ghost().caught == 0.0f);
        }
    }
}

TEST_CASE("A ghost held by a tornado neither dodges nor attacks") {
    Funnel s;
    s.player.feet = glm::vec3(0.0f, 0.0f, 6.0f);
    s.player.eye = s.player.feet + glm::vec3(0.0f, 1.62f, 0.0f);
    s.player.viewDirection = glm::vec3(0.0f, 0.0f, -1.0f);
    s.world.spawn(typeOf("wisp"), glm::vec3(3.0f, 0.6f, 0.0f));
    s.run(0.5f);
    REQUIRE(s.ghost().caught > 0.0f);
    s.events.clear();
    int shots = 0;
    for (int tick = 0; tick < 240 && s.ghost().caught > 0.0f; ++tick) {
        s.run(kDt);
        if (s.ghost().caught > 0.0f) {
            s.world.reactToShot(s.player.eye, glm::normalize(s.ghost().position - s.player.eye), true, s.events);
            ++shots;
        }
    }
    CHECK(shots > 20);
    CHECK(count<GhostDodged>(s.events) == 0);
    CHECK(count<PlayerDamaged>(s.events) == 0);
    CHECK(s.ghost().state != GhostState::Rush);
}
