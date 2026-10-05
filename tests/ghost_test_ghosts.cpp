#include "engine/assets/asset_path.h"
#include "game/ghosts/ghost_world.h"
#include "game/player/roster.h"

#include <doctest/doctest.h>

#include <cstdio>
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

GhostQuarry playerAt(const glm::vec3& feet, const glm::vec3& view = {0.0f, 0.0f, -1.0f}) {
    return {feet, feet + glm::vec3(0.0f, 1.62f, 0.0f), view, false};
}

template <typename T>
const T* find(const EventList& events) {
    for (const GameEvent& e : events) {
        if (const auto* t = std::get_if<T>(&e)) {
            return t;
        }
    }
    return nullptr;
}

struct Scene {
    GhostWorld world{data()};
    GhostQuarry player = playerAt({0.0f, 0.0f, 0.0f});
    GhostContext context;
    EventList events;

    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            context.players = std::span<const GhostQuarry>(&player, 1);
            world.tick(kDt, context, events);
        }
    }
    const Ghost& ghost() const { return world.ghosts().front(); }
    float distance() const {
        const glm::vec3 d = ghost().position - player.feet;
        return glm::length(glm::vec2(d.x, d.z));
    }
};

}

TEST_CASE("The shipped ghost data loads") {
    const GhostDef& wisp = data().types[data().type("wisp")];
    CHECK(wisp.health > 0.0f);
    CHECK(wisp.damage[static_cast<std::size_t>(DamageKind::Wind)] > wisp.damage[static_cast<std::size_t>(DamageKind::Plain)]);
    REQUIRE(wisp.drops.size() == 2);
    CHECK(ammo().materials[wisp.drops[0].material].name == "ember_ash");
    CHECK(ammo().materials[wisp.drops[1].material].name == "ectoplasm");
}

TEST_CASE("Only rounds whose effect does damage carry a damage kind") {
    CHECK(ammo().elements[ammo().element("wind")].damage == DamageKind::Wind);
    CHECK(ammo().elements[ammo().element("inferno")].damage == DamageKind::Fire);
    CHECK(ammo().elements[ammo().element("thunderstrike")].damage == DamageKind::Lightning);
    CHECK(ammo().elements[ammo().element("haze")].damage == DamageKind::Plain);
    CHECK(ammo().elements[ammo().element("fog")].damage == DamageKind::Plain);
    CHECK(ammo().elements[ammo().element("blink")].damage == DamageKind::Plain);
}

TEST_CASE("A ghost perceives a player in range and in the open, not past its range, a wall, fog or a shroud") {
    const GhostDef& def = data().types[data().type("wisp")];
    Ghost ghost;
    ghost.position = {0.0f, 1.5f, -10.0f};
    GhostQuarry player = playerAt({0.0f, 0.0f, 0.0f});
    GhostContext open;
    CHECK(ghostPerceives(ghost, def, player, open));

    Ghost far = ghost;
    far.position.z = -60.0f;
    CHECK_FALSE(ghostPerceives(far, def, player, open));

    GhostContext walled;
    walled.blocked = [](const glm::vec3&, const glm::vec3&) { return true; };
    CHECK_FALSE(ghostPerceives(ghost, def, player, walled));

    FogField fog;
    fog.beginSync();
    fog.syncCloud(1, {0.0f, 0.0f, -5.0f}, 4.0f, 3.0f, 5.0f, 15.0f);
    fog.endSync();
    GhostContext foggy;
    foggy.fog = &fog;
    CHECK_FALSE(ghostPerceives(ghost, def, player, foggy));

    player.hidden = true;
    CHECK_FALSE(ghostPerceives(ghost, def, player, open));
}

TEST_CASE("Low to the ground a player is seen only from closer: crouched less far, crawling much less") {
    const GhostDef& def = data().types[data().type("wisp")];
    GhostContext open;
    GhostQuarry player = playerAt({0.0f, 0.0f, 0.0f});
    auto seenFrom = [&](float distance, float visibility) {
        Ghost ghost;
        ghost.position = {0.0f, 1.0f, -distance};
        GhostQuarry q = player;
        q.visibility = visibility;
        return ghostPerceives(ghost, def, q, open);
    };
    const float range = def.sightRange;
    CHECK(seenFrom(range * 0.75f, 1.0f));
    CHECK_FALSE(seenFrom(range * 0.75f, 0.6f));
    CHECK(seenFrom(range * 0.5f, 0.6f));
    CHECK_FALSE(seenFrom(range * 0.5f, 0.35f));
    CHECK(seenFrom(range * 0.25f, 0.35f));
    PlayerRules rules;
    CHECK(rules.visibility(Stance::Stand) == doctest::Approx(1.0f));
    CHECK(rules.visibility(Stance::Crouch) < 1.0f);
    CHECK(rules.visibility(Stance::Crawl) < rules.visibility(Stance::Crouch));
}

TEST_CASE("An unaware wisp wanders near where it haunts") {
    Scene s;
    s.player = playerAt({0.0f, 0.0f, 200.0f});
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -10.0f});
    s.run(10.0f);
    CHECK(s.ghost().state == GhostState::Wander);
    CHECK(glm::distance(s.ghost().position, glm::vec3(0.0f, 1.5f, -10.0f)) < 6.0f);
}

TEST_CASE("A wisp lures: it hangs at its distance, and backs away when approached") {
    Scene s;
    const float lure = data().types[data().type("wisp")].wisp.lureDistance;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -12.0f});
    s.run(5.0f);
    CHECK(s.ghost().state == GhostState::Lure);
    CHECK(s.distance() == doctest::Approx(lure).epsilon(0.25));

    for (int i = 0; i < 360; ++i) {
        s.player = playerAt(s.player.feet + glm::vec3(0.0f, 0.0f, -1.5f * kDt));
        s.context.players = std::span<const GhostQuarry>(&s.player, 1);
        s.world.tick(kDt, s.context, s.events);
    }
    CHECK(s.ghost().state == GhostState::Lure);
    CHECK(s.distance() > lure * 0.6f);
    CHECK_FALSE(find<PlayerDamaged>(s.events));
}

TEST_CASE("Look away from a wisp and it rushes in and bursts on you, leaving nothing") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -8.0f});
    s.run(3.0f);
    REQUIRE(s.ghost().state == GhostState::Lure);

    s.player.viewDirection = {0.0f, 0.0f, 1.0f};
    s.run(4.0f);
    const auto* hurt = find<PlayerDamaged>(s.events);
    REQUIRE(hurt);
    CHECK(hurt->amount == doctest::Approx(data().types[data().type("wisp")].wisp.burstDamage));
    CHECK(find<GhostBurst>(s.events));
    const auto* died = find<GhostDied>(s.events);
    REQUIRE(died);
    CHECK(died->burst);
    CHECK(s.world.ghosts().empty());
}

TEST_CASE("Turn back and look at a rushing wisp before it is on you and it breaks off; too close, it comes regardless") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -8.0f});
    s.run(3.0f);
    REQUIRE(s.ghost().state == GhostState::Lure);
    s.player.viewDirection = {0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 600 && s.ghost().state != GhostState::Rush; ++i) {
        s.run(kDt);
    }
    REQUIRE(s.ghost().state == GhostState::Rush);
    s.run(0.1f);
    REQUIRE(s.distance() > data().types[data().type("wisp")].wisp.rushTrigger);
    s.player.viewDirection = {0.0f, 0.0f, -1.0f};
    s.run(0.05f);
    CHECK(s.ghost().state == GhostState::Lure);
    s.run(2.0f);
    CHECK(find<GhostBurst>(s.events) == nullptr);
    CHECK(find<PlayerDamaged>(s.events) == nullptr);
    CHECK(s.distance() > 4.0f);

    Scene t;
    t.world.spawn(data().type("wisp"), {0.0f, 1.5f, -2.0f});
    t.run(2.0f);
    CHECK(find<GhostBurst>(t.events) != nullptr);
}

TEST_CASE("Coming too close to a wisp also sets it off") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -2.0f});
    s.run(0.05f);
    CHECK(s.ghost().state == GhostState::Rush);
}

TEST_CASE("Damage follows the type's multipliers: wind snuffs a wisp, plain takes several") {
    Scene s;
    const std::uint32_t id = s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -10.0f});
    CHECK(s.world.damage(id, 1.0f, DamageKind::Plain, {0.0f, 1.5f, -9.7f}, s.events));
    CHECK(s.world.ghosts().size() == 1);
    CHECK(s.ghost().health == doctest::Approx(30.0f - 25.0f * 0.4f));
    CHECK(s.ghost().state == GhostState::Flinch);
    const auto* hurt = find<GhostHurt>(s.events);
    REQUIRE(hurt);
    CHECK_FALSE(hurt->killed);

    CHECK(s.world.damage(id, 1.0f, DamageKind::Wind, {0.0f, 1.5f, -9.7f}, s.events));
    CHECK_FALSE(find<GhostDied>(s.events));
    CHECK_FALSE(s.world.damage(id, 1.0f, DamageKind::Wind, {}, s.events));
    s.run(0.3f);
    const auto* died = find<GhostDied>(s.events);
    REQUIRE(died);
    CHECK_FALSE(died->burst);
    CHECK(s.world.ghosts().empty());
    CHECK_FALSE(s.world.damage(id, 1.0f, DamageKind::Wind, {}, s.events));
}

TEST_CASE("A hit holds a ghost still for a moment, a kill for longer, and then it carries on") {
    const GhostDef& def = data().types[data().type("wisp")];
    Scene s;
    s.player = playerAt({0.0f, 0.0f, 200.0f});
    const std::uint32_t id = s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -10.0f});
    s.run(0.5f);

    s.world.damage(id, 0.2f, DamageKind::Plain, {0.0f, 1.5f, -9.7f}, s.events);
    const auto* hurt = find<GhostHurt>(s.events);
    REQUIRE(hurt);
    const float graze = hurt->hitstop;
    CHECK(graze >= def.hitstopMin);
    CHECK(graze < def.hitstopMax);
    CHECK(s.ghost().hitstop == doctest::Approx(graze));

    const glm::vec3 at = s.ghost().position;
    const float age = s.ghost().age;
    s.run(graze * 0.6f);
    CHECK(s.ghost().position == at);
    CHECK(s.ghost().age == age);

    const float left = s.ghost().hitstop;
    const float health = s.ghost().health;
    s.world.damage(id, 0.2f, DamageKind::Plain, {0.0f, 1.5f, -9.7f}, s.events);
    CHECK(s.ghost().health < health);
    CHECK(s.ghost().hitstop == doctest::Approx(left));

    s.run(graze);
    CHECK(s.ghost().hitstop == 0.0f);
    CHECK(glm::distance(s.ghost().position, at) > 0.05f);

    s.events.clear();
    s.world.damage(id, 5.0f, DamageKind::Wind, {0.0f, 1.5f, -9.7f}, s.events);
    const auto* fatal = find<GhostHurt>(s.events);
    REQUIRE(fatal);
    CHECK(fatal->killed);
    CHECK(fatal->hitstop == doctest::Approx(def.hitstopMax));
    CHECK_FALSE(s.world.raycast({0.0f, 1.5f, 0.0f}, s.ghost().position + glm::vec3(0.0f, 0.0f, -5.0f)));
    s.run(def.hitstopMax * 0.5f);
    CHECK(s.world.ghosts().size() == 1);
    s.run(def.hitstopMax);
    CHECK(s.world.ghosts().empty());
}

TEST_CASE("Something that keeps hurting a ghost only holds it on first contact") {
    Scene s;
    s.player = playerAt({0.0f, 0.0f, 200.0f});
    const std::uint32_t id = s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -10.0f});
    auto countHurt = [&] {
        int n = 0;
        for (const GameEvent& e : s.events) {
            n += std::holds_alternative<GhostHurt>(e) ? 1 : 0;
        }
        return n;
    };
    auto burn = [&](float seconds, std::uint32_t source) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            s.world.damage(id, 0.05f * kDt, DamageKind::Fire, s.ghost().position, s.events, source);
            s.run(kDt);
        }
    };
    burn(1.0f, 7);
    CHECK(countHurt() == 1);
    CHECK(s.ghost().hitstop == 0.0f);
    CHECK(s.ghost().state != GhostState::Flinch);

    burn(0.3f, 8);
    CHECK(countHurt() == 2);

    s.run(1.0f);
    burn(0.1f, 7);
    CHECK(countHurt() == 3);
}

TEST_CASE("A bullet's path finds the ghost it passes through") {
    Scene s;
    const std::uint32_t id = s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -10.0f});
    const auto hit = s.world.raycast({0.0f, 1.5f, 0.0f}, {0.0f, 1.5f, -30.0f});
    REQUIRE(hit);
    CHECK(hit->id == id);
    CHECK(hit->point.z == doctest::Approx(-10.0f + data().types[0].radius).epsilon(0.01));
    CHECK_FALSE(s.world.raycast({3.0f, 1.5f, 0.0f}, {3.0f, 1.5f, -30.0f}));
}

TEST_CASE("A wisp that sees the shot coming may slip aside; stealthy or instant shots give it no chance") {
    Scene s;
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -8.0f});
    s.run(0.3f);
    int dodged = 0;
    for (int shot = 0; shot < 40; ++shot) {
        REQUIRE_FALSE(s.world.ghosts().empty());
        INFO("shot " << shot << " state " << ghostStateName(s.ghost().state) << " at " << s.ghost().position.x << ", "
                     << s.ghost().position.y << ", " << s.ghost().position.z);
        const glm::vec3 muzzle{0.0f, s.ghost().position.y, 0.0f};
        const glm::vec3 aim = glm::normalize(s.ghost().position - muzzle);
        EventList events;
        s.world.reactToShot(muzzle, aim, true, events);
        if (find<GhostDodged>(events)) {
            ++dodged;
            CHECK_FALSE(s.world.raycast(muzzle, muzzle + aim * 30.0f));
        } else {
            CHECK(s.world.raycast(muzzle, muzzle + aim * 30.0f));
        }
        s.player.viewDirection = glm::normalize(s.ghost().position - s.player.eye);
        s.run(0.5f);
    }
    CHECK(dodged > 5);
    CHECK(dodged < 35);

    Scene stealth;
    stealth.world.spawn(data().type("wisp"), {0.0f, 1.5f, -8.0f});
    stealth.run(0.3f);
    for (int i = 0; i < 40; ++i) {
        stealth.world.reactToShot({0.0f, 1.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, false, stealth.events);
    }
    CHECK_FALSE(find<GhostDodged>(stealth.events));
}

TEST_CASE("A gunshot draws a wisp that did not see you") {
    Scene s;
    s.context.blocked = [](const glm::vec3&, const glm::vec3&) { return true; };
    s.world.spawn(data().type("wisp"), {0.0f, 1.5f, -20.0f});
    s.run(1.0f);
    REQUIRE(s.ghost().state == GhostState::Wander);
    s.world.reactToShot({0.0f, 1.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, true, s.events);
    s.run(1.5f);
    CHECK(s.ghost().position.z > -18.5f);
}

TEST_CASE("A blast shoves a wisp away") {
    Scene s;
    s.player = playerAt({0.0f, 0.0f, 200.0f});
    s.world.spawn(data().type("wisp"), {1.0f, 1.5f, -10.0f});
    s.world.push({0.0f, 1.5f, -10.0f}, 4.0f, 8.0f);
    CHECK(s.ghost().velocity.x > 3.0f);
}

namespace {
struct Haunt {
    GhostWorld world{data()};
    GhostQuarry player = playerAt({0.0f, 0.0f, 0.0f});
    std::vector<GhostProp> props{{7, {1.0f, 0.25f, -9.0f}, 6.0f}};
    std::vector<std::pair<std::uint32_t, glm::vec3>> moved;
    EventList events;

    void run(float seconds) {
        GhostContext context;
        context.props = props;
        context.moveProp = [this](std::uint32_t body, const glm::vec3& velocity) {
            moved.emplace_back(body, velocity);
            for (GhostProp& prop : props) {
                if (prop.body == body) {
                    prop.position += velocity * kDt;
                }
            }
        };
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            context.players = std::span<const GhostQuarry>(&player, 1);
            world.tick(kDt, context, events);
        }
    }
    const Ghost& ghost() const { return world.ghosts().front(); }
};

}

TEST_CASE("The poltergeist data loads: unseen, and it drops wind") {
    const GhostDef& def = data().types[data().type("poltergeist")];
    CHECK(def.invisible);
    CHECK(def.behavior == GhostBehavior::Poltergeist);
    REQUIRE(def.drops.size() == 1);
    CHECK(ammo().materials[def.drops[0].material].name == "gale_wisp");
}

TEST_CASE("A poltergeist that notices someone lifts a loose thing and hurls it at them") {
    Haunt h;
    h.world.spawn(data().type("poltergeist"), {0.0f, 1.6f, -9.0f});
    h.run(0.5f);
    CHECK(h.ghost().state == GhostState::Lift);
    CHECK(h.ghost().heldProp == 7);
    REQUIRE_FALSE(h.moved.empty());
    CHECK(h.props[0].position.y > 0.4f);
    CHECK_FALSE(find<GhostThrew>(h.events));

    h.run(1.0f);
    const auto* threw = find<GhostThrew>(h.events);
    REQUIRE(threw);
    CHECK(threw->body == 7);
    CHECK(threw->velocity.z > 8.0f);
    CHECK(glm::length(threw->velocity) > 12.0f);
    CHECK(h.ghost().state == GhostState::Recover);
    CHECK(h.ghost().heldProp == kNoProp);

    h.props[0].position = {1.0f, 0.25f, -9.0f};
    h.run(data().types[data().type("poltergeist")].poltergeist.cooldown + 0.3f);
    CHECK(h.ghost().state == GhostState::Lift);
}

TEST_CASE("A poltergeist that has noticed no one leaves things alone, and one that is hurt drops what it holds") {
    Haunt quiet;
    quiet.player.hidden = true;
    quiet.world.spawn(data().type("poltergeist"), {0.0f, 1.6f, -9.0f});
    quiet.run(3.0f);
    CHECK(quiet.ghost().state == GhostState::Wander);
    CHECK(quiet.moved.empty());

    Haunt h;
    const std::uint32_t id = h.world.spawn(data().type("poltergeist"), {0.0f, 1.6f, -9.0f});
    h.run(0.5f);
    REQUIRE(h.ghost().state == GhostState::Lift);
    h.world.damage(id, 1.0f, DamageKind::Plain, h.ghost().position, h.events);
    h.run(0.1f);
    CHECK(h.ghost().state == GhostState::Flinch);
    CHECK(h.ghost().heldProp == kNoProp);
    CHECK_FALSE(find<GhostThrew>(h.events));
}

TEST_CASE("With nothing in reach, a poltergeist goes to where there is something to throw") {
    Haunt h;
    h.props[0].position = {14.0f, 0.25f, -9.0f};
    h.world.spawn(data().type("poltergeist"), {0.0f, 1.6f, -9.0f});
    h.run(4.0f);
    CHECK(h.ghost().position.x > 5.0f);
}
