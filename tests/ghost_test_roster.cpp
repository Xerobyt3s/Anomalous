#include "engine/assets/asset_path.h"
#include "game/ballistics/ballistics.h"
#include "game/ammo/ammo_data.h"
#include "game/player/player_hit.h"
#include "game/player/roster.h"
#include "game/ghosts/ghost_poses.h"

#include <doctest/doctest.h>

#include <cmath>
#include <variant>
#include <vector>

using namespace ghost::game;

namespace {
constexpr float kDt = 1.0f / 120.0f;

template <typename T>
int count(const EventList& events) {
    int n = 0;
    for (const GameEvent& e : events) {
        n += std::holds_alternative<T>(e) ? 1 : 0;
    }
    return n;
}

template <typename T>
const T* last(const EventList& events) {
    const T* found = nullptr;
    for (const GameEvent& e : events) {
        if (const auto* t = std::get_if<T>(&e)) {
            found = t;
        }
    }
    return found;
}

PlayerBody standing(PlayerId id, const glm::vec3& feet) {
    PlayerBody b;
    b.id = id;
    b.feet = feet;
    b.eye = feet + glm::vec3(0.0f, 1.62f, 0.0f);
    return b;
}

struct Party {
    PlayerRules rules;
    Roster roster{rules};
    std::vector<RosterInput> inputs;
    EventList events;

    explicit Party(int players) {
        for (int i = 0; i < players; ++i) {
            roster.add(static_cast<PlayerId>(i));
            inputs.push_back({static_cast<PlayerId>(i), glm::vec3(static_cast<float>(i), 0.0f, 0.0f), false});
        }
    }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            roster.tick(kDt, inputs, events);
        }
    }
};

}

TEST_CASE("The shipped player rules load") {
    const PlayerRules rules = loadPlayerRules(ghost::engine::assetPath("data"));
    CHECK(rules.headDamage > rules.bodyDamage);
    CHECK(rules.roundDamage(DamageKind::Plain, false) == doctest::Approx(rules.bodyDamage));
    CHECK(rules.roundDamage(DamageKind::Lightning, true) > rules.headDamage);
    CHECK(rules.friendlyFire);
    CHECK(rules.reviveTime > 0.0f);
    CHECK_THROWS(parsePlayerRules(R"({"revive": {"time": 0}})"));
}

TEST_CASE("A line through a standing player hits the body, or the head if it reaches it") {
    const std::vector<PlayerBody> players{standing(1, {0.0f, 0.0f, -10.0f})};
    const auto chest = raycastPlayers(players, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -20.0f});
    REQUIRE(chest);
    CHECK(chest->id == 1);
    CHECK_FALSE(chest->head);
    CHECK(chest->point.z == doctest::Approx(-9.7f).epsilon(0.01));
    CHECK(chest->normal.z > 0.9f);

    const auto head = raycastPlayers(players, {0.0f, 1.45f, 0.0f}, {0.0f, 1.45f, -20.0f});
    REQUIRE(head);
    CHECK(head->head);

    CHECK_FALSE(raycastPlayers(players, {0.0f, 1.9f, 0.0f}, {0.0f, 1.9f, -20.0f}));
    CHECK_FALSE(raycastPlayers(players, {0.5f, 1.0f, 0.0f}, {0.5f, 1.0f, -20.0f}));
    CHECK_FALSE(raycastPlayers(players, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -5.0f}));
    CHECK_FALSE(raycastPlayers(players, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -20.0f}, 1));
}

TEST_CASE("A crouched or downed player is a smaller target, and the nearest player is the one hit") {
    PlayerBody crouched = standing(1, {0.0f, 0.0f, -10.0f});
    crouched.height = 1.1f;
    const std::vector<PlayerBody> low{crouched};
    CHECK_FALSE(raycastPlayers(low, {0.0f, 1.4f, 0.0f}, {0.0f, 1.4f, -20.0f}));
    const auto head = raycastPlayers(low, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -20.0f});
    REQUIRE(head);
    CHECK(head->head);

    PlayerBody down = standing(2, {0.0f, 0.0f, -10.0f});
    down.height = 0.5f;
    down.downed = true;
    const std::vector<PlayerBody> lying{down};
    CHECK_FALSE(raycastPlayers(lying, {0.0f, 0.8f, 0.0f}, {0.0f, 0.8f, -20.0f}));
    const auto body = raycastPlayers(lying, {0.0f, 0.3f, 0.0f}, {0.0f, 0.3f, -20.0f});
    REQUIRE(body);
    CHECK_FALSE(body->head);

    const std::vector<PlayerBody> line{standing(1, {0.0f, 0.0f, -10.0f}), standing(2, {0.0f, 0.0f, -5.0f})};
    const auto first = raycastPlayers(line, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, -20.0f});
    REQUIRE(first);
    CHECK(first->id == 2);
}

TEST_CASE("Health comes back when left alone; enough harm puts a player down") {
    Party party(2);
    party.roster.hurt(0, 0.5f, {}, party.events);
    CHECK(party.roster.find(0)->health == doctest::Approx(0.5f));
    REQUIRE(last<PlayerDamaged>(party.events));
    CHECK(last<PlayerDamaged>(party.events)->player == 0);
    party.run(2.0f);
    CHECK(party.roster.find(0)->health == doctest::Approx(0.5f));
    party.run(4.0f);
    CHECK(party.roster.find(0)->health > 0.65f);

    party.roster.hurt(0, 2.0f, {}, party.events);
    CHECK(party.roster.downed(0));
    REQUIRE(last<PlayerDowned>(party.events));
    CHECK(last<PlayerDowned>(party.events)->player == 0);

    const int hurts = count<PlayerDamaged>(party.events);
    party.roster.hurt(0, 0.5f, {}, party.events);
    CHECK(count<PlayerDamaged>(party.events) == hurts);
    party.run(10.0f);
    CHECK(party.roster.downed(0));
    CHECK(count<PlayerDied>(party.events) == 0);
}

TEST_CASE("A teammate in reach holding on for the full time gets a downed player up; letting go starts over") {
    Party party(2);
    party.roster.hurt(0, 2.0f, {}, party.events);
    REQUIRE(party.roster.downed(0));

    party.inputs[1].interact = true;
    party.run(party.rules.reviveTime * 0.6f);
    CHECK(party.roster.downed(0));
    CHECK(party.roster.find(0)->revive == doctest::Approx(0.6f).epsilon(0.05));
    CHECK(party.roster.find(0)->reviver == 1);

    party.inputs[1].interact = false;
    party.run(0.1f);
    CHECK(party.roster.find(0)->revive == 0.0f);

    party.inputs[1].interact = true;
    party.inputs[1].position = {10.0f, 0.0f, 0.0f};
    party.run(party.rules.reviveTime + 0.5f);
    CHECK(party.roster.downed(0));

    party.inputs[1].position = {1.0f, 0.0f, 0.0f};
    party.run(party.rules.reviveTime + 0.1f);
    CHECK_FALSE(party.roster.downed(0));
    CHECK(party.roster.find(0)->health == doctest::Approx(party.rules.reviveHealth));
    REQUIRE(last<PlayerRevived>(party.events));
    CHECK(last<PlayerRevived>(party.events)->player == 0);
    CHECK(last<PlayerRevived>(party.events)->by == 1);
}

TEST_CASE("With everyone down the hunt starts over; alone, that is at once") {
    Party party(2);
    party.roster.hurt(0, 2.0f, {}, party.events);
    party.inputs[0].interact = true;
    party.run(1.0f);
    CHECK(count<PlayerDied>(party.events) == 0);
    party.roster.hurt(1, 2.0f, {}, party.events);
    party.run(kDt * 2.0f);
    CHECK(count<PlayerDied>(party.events) == 1);
    CHECK_FALSE(party.roster.downed(0));
    CHECK_FALSE(party.roster.downed(1));
    CHECK(party.roster.find(0)->health == 1.0f);

    Party solo(1);
    solo.roster.hurt(0, 2.0f, {}, solo.events);
    solo.run(kDt * 2.0f);
    CHECK(count<PlayerDied>(solo.events) == 1);
    CHECK_FALSE(solo.roster.downed(0));
}

TEST_CASE("Burning hurts every tick but is only told a few times a second") {
    Party party(2);
    for (int i = 0; i < 120; ++i) {
        party.roster.hurt(0, party.rules.burnPerSecond * kDt, {}, party.events, true);
        party.roster.tick(kDt, party.inputs, party.events);
    }
    CHECK(party.roster.find(0)->health == doctest::Approx(1.0f - party.rules.burnPerSecond).epsilon(0.02));
    CHECK(count<PlayerDamaged>(party.events) >= 2);
    CHECK(count<PlayerDamaged>(party.events) <= 4);
    float told = 0.0f;
    for (const GameEvent& e : party.events) {
        if (const auto* d = std::get_if<PlayerDamaged>(&e)) {
            told += d->amount;
        }
    }
    CHECK(told == doctest::Approx(party.rules.burnPerSecond).epsilon(0.3));
}

TEST_CASE("A bullet stops in a player it is allowed to hit, and reports whose it was") {
    const std::vector<PlayerBody> players{standing(0, {0.0f, 0.0f, 0.0f}), standing(1, {0.0f, 0.0f, -10.0f})};
    const RaycastFn nothing = [](const glm::vec3&, const glm::vec3&) -> std::optional<ghost::engine::RayHit> { return std::nullopt; };
    const ProjectileHitFn bodies = [&](const Projectile& p, const glm::vec3& from, const glm::vec3& to) -> std::optional<ghost::engine::RayHit> {
        const auto hit = raycastPlayers(players, from, to, p.owner);
        if (!hit) {
            return std::nullopt;
        }
        ghost::engine::RayHit out;
        out.point = hit->point;
        out.normal = hit->normal;
        out.fraction = hit->fraction;
        out.userData = static_cast<std::uint64_t>(Surface::Player);
        out.body = hit->id;
        return out;
    };

    Ballistics b;
    EventList events;
    ShotProfile mine;
    mine.owner = 0;
    b.fire({0.0f, 1.2f, 0.0f}, {0.0f, 0.0f, -1.0f}, Round{}, mine);
    for (int i = 0; i < 30 && events.empty(); ++i) {
        b.tick(kDt, nothing, events, {}, {}, bodies);
    }
    REQUIRE(events.size() == 1);
    const auto& impact = std::get<ProjectileImpact>(events[0]);
    CHECK(impact.surface == Surface::Player);
    CHECK(impact.body == 1);
    CHECK(impact.shooter == 0);
    CHECK(impact.point.z == doctest::Approx(-9.7f).epsilon(0.01));
    b.tick(kDt, nothing, events, {}, {}, bodies);
    CHECK(b.projectiles().empty());
}

TEST_CASE("In the arena nobody is revived: the downed come back by themselves, and the one who did it is credited") {
    Party party(2);
    party.rules.arena = true;
    party.roster.hurt(0, 0.6f, {}, party.events, false, 1);
    CHECK(party.roster.find(1)->kills == 0);
    party.run(10.0f);
    CHECK(party.roster.find(0)->health == doctest::Approx(0.4f));
    party.roster.hurt(0, 0.6f, {}, party.events, false, 1);
    REQUIRE(party.roster.downed(0));
    CHECK(last<PlayerDowned>(party.events)->by == 1);
    CHECK(party.roster.find(1)->kills == 1);

    party.inputs[1].interact = true;
    party.run(party.rules.arenaRespawn * 0.5f);
    CHECK(party.roster.downed(0));
    CHECK(party.roster.find(0)->revive == 0.0f);
    party.run(party.rules.arenaRespawn * 0.6f);
    CHECK_FALSE(party.roster.downed(0));
    CHECK(party.roster.find(0)->health == 1.0f);
    REQUIRE(last<PlayerRespawned>(party.events));
    CHECK(last<PlayerRespawned>(party.events)->player == 0);
    CHECK(count<PlayerRevived>(party.events) == 0);

    party.roster.hurt(0, 2.0f, {}, party.events, false, 0);
    party.roster.hurt(1, 2.0f, {}, party.events);
    party.run(0.1f);
    CHECK(count<PlayerDied>(party.events) == 0);
    CHECK(party.roster.find(0)->kills == 0);
    CHECK(party.roster.find(1)->kills == 1);

    party.roster.reset();
    CHECK(party.roster.find(1)->kills == 0);
    CHECK_FALSE(party.roster.downed(0));
    CHECK(party.roster.find(0)->health == 1.0f);
}

TEST_CASE("A pressure burst throws those near away from it: straight up from under the feet, less further off, nothing out of reach") {
    const PlayerBody body = standing(0, {0.0f, 0.0f, 0.0f});

    const glm::vec3 up = blastShove({0.0f, 0.1f, 0.0f}, body, 3.5f, 11.0f);
    CHECK(up.y > 9.0f);
    CHECK(std::abs(up.x) < 0.01f);

    const glm::vec3 nearBy = blastShove({1.0f, 0.9f, 0.0f}, body, 3.5f, 11.0f);
    const glm::vec3 farOff = blastShove({3.0f, 0.9f, 0.0f}, body, 3.5f, 11.0f);
    CHECK(nearBy.x < -5.0f);
    CHECK(farOff.x < 0.0f);
    CHECK(glm::length(farOff) < glm::length(nearBy));
    CHECK(glm::length(farOff) > 11.0f * 0.5f);
    CHECK(glm::length(blastShove({4.0f, 0.9f, 0.0f}, body, 3.5f, 11.0f)) == 0.0f);

    const AmmoData ammo = loadAmmoData(ghost::engine::assetPath("data"));
    CHECK(ammo.elements[ammo.element("wind")].blastRadius > 2.0f);
    CHECK(ammo.elements[ammo.element("wind")].blastKnockback > 3.0f);
    CHECK(ammo.elements[ammo.element("inferno")].blastRadius == 0.0f);
}

TEST_CASE("The arena hat goes only to a player far enough ahead of everyone else") {
    const auto entry = [](PlayerId id, int kills, bool zombie = false) {
        RosterEntry e;
        e.id = id;
        e.kills = kills;
        e.zombie = zombie;
        return e;
    };
    const std::vector<RosterEntry> alone{entry(0, 5)};
    CHECK(dominantLeader(alone, 5) == std::optional<PlayerId>(PlayerId{0}));
    const std::vector<RosterEntry> short_of_it{entry(0, 4)};
    CHECK_FALSE(dominantLeader(short_of_it, 5).has_value());
    const std::vector<RosterEntry> ahead{entry(0, 2), entry(1, 7), entry(2, 1)};
    CHECK(dominantLeader(ahead, 5) == std::optional<PlayerId>(PlayerId{1}));
    const std::vector<RosterEntry> close{entry(0, 2), entry(1, 6)};
    CHECK_FALSE(dominantLeader(close, 5).has_value());
    const std::vector<RosterEntry> tied{entry(0, 9), entry(1, 9), entry(2, 0)};
    CHECK_FALSE(dominantLeader(tied, 5).has_value());
    const std::vector<RosterEntry> haunted{entry(0, 6), entry(kZombieIdBase, 20, true)};
    CHECK(dominantLeader(haunted, 5) == std::optional<PlayerId>(PlayerId{0}));
}

TEST_CASE("Every kind of ghost has at least one pose to inspect") {
    for (GhostBehavior b : {GhostBehavior::Wisp, GhostBehavior::Poltergeist, GhostBehavior::BallLightning, GhostBehavior::Mimic,
                            GhostBehavior::Vasskraka, GhostBehavior::Necromite}) {
        CHECK_FALSE(posesOf(b).empty());
    }
}
