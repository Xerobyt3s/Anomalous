#include "engine/assets/asset_path.h"
#include "game/ghosts/ghost_world.h"

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

struct Tilted {
    glm::vec3 up = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    GhostWorld world{data()};
    GhostQuarry player;
    GhostContext context;
    EventList events;

    Tilted() {
        context.gravity = [this](const glm::vec3&) { return -up * 9.81f; };
        context.raycast = [this](const glm::vec3& from, const glm::vec3& to) -> std::optional<GhostSurfaceHit> {
            const float a = glm::dot(from, up);
            const float b = glm::dot(to, up);
            if ((a >= 0.0f) == (b >= 0.0f) || std::abs(a - b) < 1e-6f) {
                return std::nullopt;
            }
            return GhostSurfaceHit{glm::mix(from, to, a / (a - b)), a >= 0.0f ? up : -up};
        };
        place(glm::vec3(0.0f, 0.0f, 6.0f));
    }
    void place(const glm::vec3& feet, const glm::vec3& view = {0.0f, 0.0f, -1.0f}) {
        player.feet = feet;
        player.eye = feet + up * 1.62f;
        player.up = up;
        player.viewDirection = view;
    }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            context.players = std::span<const GhostQuarry>(&player, 1);
            world.tick(kDt, context, events);
        }
    }
    const Ghost& ghost() const { return world.ghosts().front(); }
    float height() const { return glm::dot(ghost().position, up); }
};

struct Globe {
    float radius = 30.0f;
    GhostWorld world{data()};
    GhostQuarry player;
    GhostContext context;
    EventList events;

    Globe() {
        context.gravity = [](const glm::vec3& at) { return -glm::normalize(at) * 9.81f; };
        context.raycast = [this](const glm::vec3& from, const glm::vec3& to) -> std::optional<GhostSurfaceHit> {
            const glm::vec3 d = to - from;
            const float a = glm::dot(d, d);
            const float b = glm::dot(from, d);
            const float c = glm::dot(from, from) - radius * radius;
            const float disc = b * b - a * c;
            if (a < 1e-9f || disc < 0.0f) {
                return std::nullopt;
            }
            const float t = (-b - std::sqrt(disc)) / a;
            if (t < 0.0f || t > 1.0f) {
                return std::nullopt;
            }
            const glm::vec3 point = from + d * t;
            return GhostSurfaceHit{point, glm::normalize(point)};
        };
    }
    glm::vec3 onSurface(float angle) const { return glm::vec3(std::sin(angle), std::cos(angle), 0.0f) * radius; }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            context.players = std::span<const GhostQuarry>(&player, 1);
            world.tick(kDt, context, events);
        }
    }
};

}

TEST_CASE("Under tilted gravity a ghost takes the field's up") {
    Tilted s;
    s.world.spawn(typeOf("wisp"), s.up * 3.0f);
    s.run(2.0f);
    CHECK(glm::dot(s.ghost().up, s.up) > 0.999f);
}

TEST_CASE("A mimic dropped above a tilted floor lands on it and sticks along the field's up") {
    Tilted s;
    s.place(glm::vec3(0.0f, 0.0f, 40.0f));
    s.world.spawn(typeOf("mimic"), s.up * 2.0f, s.up);
    s.run(2.0f);
    CHECK(s.ghost().state == GhostState::Disguised);
    CHECK(s.height() == doctest::Approx(data().types[typeOf("mimic")].mimic.bodyRadius).epsilon(0.05));
    CHECK(glm::dot(s.ghost().surfaceNormal, s.up) > 0.99f);
}

TEST_CASE("Ball lightning hovers at its height along a tilted up") {
    Tilted s;
    s.place(glm::vec3(0.0f, 0.0f, 80.0f));
    const GhostDef& def = data().types[typeOf("ball_lightning")];
    s.world.spawn(typeOf("ball_lightning"), s.up * 6.0f, s.up);
    s.run(6.0f);
    CHECK(std::abs(s.height() - def.ballLightning.hoverHeight) < 0.5f);
}

TEST_CASE("A luring wisp hangs at its hover height above the player along the player's up") {
    Tilted s;
    const GhostDef& def = data().types[typeOf("wisp")];
    s.place(glm::vec3(0.0f), glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f)));
    s.world.spawn(typeOf("wisp"), glm::vec3(0.0f, 0.0f, -5.0f) + s.up * 1.5f, s.up);
    s.run(0.5f);
    REQUIRE(s.ghost().state == GhostState::Lure);
    s.place(glm::vec3(0.0f), glm::normalize(s.ghost().position - s.player.eye));
    s.run(3.0f);
    REQUIRE(s.ghost().state == GhostState::Lure);
    const float above = glm::dot(s.ghost().position - s.player.feet, s.up);
    CHECK(std::abs(above - def.wisp.hoverHeight) < 0.4f);
}

TEST_CASE("A necromite crawls over curved ground to a downed player, staying on the surface") {
    Globe s;
    const GhostDef& def = data().types[typeOf("necromite")];
    const glm::vec3 feet = s.onSurface(0.5f);
    s.player.feet = feet;
    s.player.up = glm::normalize(feet);
    s.player.eye = feet + s.player.up * 0.35f;
    s.player.downed = true;
    s.player.id = 3;
    const glm::vec3 start = s.onSurface(0.0f);
    s.world.spawn(typeOf("necromite"), start + glm::normalize(start) * 0.2f, glm::normalize(start));
    const float before = glm::distance(start, feet);
    float worst = 0.0f;
    for (int i = 0; i < 40 && !s.world.ghosts().empty(); ++i) {
        s.run(0.25f);
        if (!s.world.ghosts().empty()) {
            worst = std::max(worst, std::abs(glm::length(s.world.ghosts().front().position) - (s.radius + def.radius)));
        }
    }
    CHECK(worst < 0.1f);
    const bool entered = count<NecromiteEntered>(s.events) > 0;
    const bool closer = !s.world.ghosts().empty() && glm::distance(s.world.ghosts().front().position, feet) < before * 0.5f;
    CHECK((entered || closer));
}

TEST_CASE("A ghost in the middle of an attack does not dodge") {
    GhostDef def;
    def.behavior = GhostBehavior::Wisp;
    Ghost ghost;
    ghost.state = GhostState::Rush;
    CHECK(ghostAttacking(ghost, def));
    ghost.state = GhostState::Lure;
    CHECK_FALSE(ghostAttacking(ghost, def));
    def.behavior = GhostBehavior::Mimic;
    ghost.state = GhostState::Leap;
    CHECK(ghostAttacking(ghost, def));
    ghost.state = GhostState::Hunt;
    CHECK_FALSE(ghostAttacking(ghost, def));
    def.behavior = GhostBehavior::Vasskraka;
    ghost.state = GhostState::Stoop;
    CHECK(ghostAttacking(ghost, def));

    GhostWorld world{data()};
    GhostQuarry player{glm::vec3(0.0f), glm::vec3(0.0f, 1.62f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), false};
    GhostContext context;
    EventList events;
    world.spawn(typeOf("wisp"), glm::vec3(0.0f, 1.5f, -5.0f));
    bool rushing = false;
    for (int i = 0; i < 1200 && !rushing; ++i) {
        context.players = std::span<const GhostQuarry>(&player, 1);
        world.tick(kDt, context, events);
        rushing = !world.ghosts().empty() && world.ghosts().front().state == GhostState::Rush;
    }
    REQUIRE(rushing);
    events.clear();
    for (int shot = 0; shot < 60; ++shot) {
        const glm::vec3 at = world.ghosts().front().position;
        world.reactToShot(player.eye, glm::normalize(at - player.eye), true, events);
    }
    CHECK(count<GhostDodged>(events) == 0);
}
