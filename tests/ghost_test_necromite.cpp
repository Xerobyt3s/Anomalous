#include "engine/assets/asset_path.h"
#include "game/fx/necromite_fx.h"
#include "game/ghosts/ghost_world.h"
#include "game/net/protocol.h"
#include "game/player/roster.h"
#include "game/player/zombie.h"

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

const AmmoData& ammo() {
    static const AmmoData d = loadAmmoData(ghost::engine::assetPath("data"));
    return d;
}
const GhostData& ghosts() {
    static const GhostData d = loadGhostData(ghost::engine::assetPath("data"), ammo());
    return d;
}
const GhostDef& wormDef() { return ghosts().types[ghosts().type("necromite")]; }

GhostQuarry standingAt(PlayerId id, const glm::vec3& feet) {
    GhostQuarry q{feet, feet + glm::vec3(0.0f, 1.32f, 0.0f), {0.0f, 0.0f, -1.0f}, false};
    q.id = id;
    return q;
}

GhostQuarry downAt(PlayerId id, const glm::vec3& feet) {
    GhostQuarry q{feet, feet + glm::vec3(0.0f, 0.35f, 0.0f), {0.0f, 0.0f, -1.0f}, true};
    q.id = id;
    q.downed = true;
    return q;
}

struct Burial {
    GhostWorld world{ghosts()};
    std::vector<GhostQuarry> players;
    GhostContext context;
    EventList events;

    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            context.players = players;
            world.tick(kDt, context, events);
        }
    }
};

struct Trio {
    PlayerRules rules;
    Roster roster{rules};
    std::vector<RosterInput> inputs;
    EventList events;

    Trio() {
        for (int i = 0; i < 3; ++i) {
            roster.add(static_cast<PlayerId>(i));
            inputs.push_back({static_cast<PlayerId>(i), glm::vec3(static_cast<float>(i), 0.0f, 0.0f), false});
        }
        roster.hurt(0, 2.0f, glm::vec3(0.0f), events);
    }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            roster.tick(kDt, inputs, events);
        }
    }
};

constexpr PlayerId kZombie = kZombieIdBase;

}

TEST_CASE("A Necromite comes up, crawls to a downed player's head and goes in; it wants nothing with the living") {
    const NecromiteParams& p = wormDef().necromite;
    Burial b;
    b.players = {downAt(0, {0.0f, 0.0f, 0.0f}), standingAt(1, {3.0f, 0.0f, 4.0f})};
    const auto id = b.world.spawn(ghosts().type("necromite"), {8.0f, 0.2f, 0.0f});
    b.world.setOn(id, 0);
    b.run(p.emergeTime * 0.5f);
    CHECK(b.world.find(id)->state == GhostState::Emerge);
    CHECK(count<NecromiteEmerged>(b.events) == 1);
    CHECK(glm::distance(b.world.find(id)->position, glm::vec3(8.0f, 0.2f, 0.0f)) < 0.05f);
    b.run(p.emergeTime + 1.0f);
    REQUIRE(b.world.find(id) != nullptr);
    CHECK(b.world.find(id)->state == GhostState::Squirm);
    CHECK(b.world.find(id)->position.x < 7.0f);
    CHECK(std::abs(b.world.find(id)->position.z) < 0.2f);
    CHECK(b.world.find(id)->position.y == doctest::Approx(wormDef().radius).epsilon(0.05));
    CHECK(count<PlayerDamaged>(b.events) == 0);
    b.run(8.0f / p.crawlSpeed + p.enterTime + 0.5f);
    CHECK(b.world.find(id) == nullptr);
    REQUIRE(count<NecromiteEntered>(b.events) == 1);
    for (const GameEvent& e : b.events) {
        if (const auto* in = std::get_if<NecromiteEntered>(&e)) {
            CHECK(in->player == 0);
        }
    }
    CHECK(count<GhostDied>(b.events) == 0);
}

TEST_CASE("Any hit kills a Necromite; and one whose body is got up first goes for another, or back into the ground") {
    {
        Burial b;
        b.players = {downAt(0, {0.0f, 0.0f, 0.0f}), standingAt(1, {3.0f, 0.0f, 4.0f})};
        const auto id = b.world.spawn(ghosts().type("necromite"), {8.0f, 0.2f, 0.0f});
        b.run(1.5f);
        const glm::vec3 at = b.world.find(id)->position;
        REQUIRE(b.world.raycast(at + glm::vec3(0.0f, 2.0f, 0.0f), at - glm::vec3(0.0f, 2.0f, 0.0f)).has_value());
        b.world.damage(id, 0.1f, DamageKind::Plain, at, b.events);
        b.run(0.3f);
        CHECK(b.world.find(id) == nullptr);
        CHECK(count<GhostDied>(b.events) == 1);
        CHECK(count<NecromiteEntered>(b.events) == 0);
    }
    {
        Burial b;
        b.players = {downAt(0, {0.0f, 0.0f, 0.0f}), downAt(1, {8.0f, 0.0f, 6.0f})};
        const auto id = b.world.spawn(ghosts().type("necromite"), {8.0f, 0.2f, 0.0f});
        b.world.setOn(id, 0);
        b.run(1.5f);
        b.players[0] = standingAt(0, {0.0f, 0.0f, 0.0f});
        b.run(1.0f);
        REQUIRE(b.world.find(id) != nullptr);
        CHECK(b.world.find(id)->prefers == 1);
        b.players[1].possessed = true;
        b.run(wormDef().necromite.emergeTime + 0.5f);
        CHECK(b.world.find(id) == nullptr);
        CHECK(count<NecromiteEntered>(b.events) == 0);
        CHECK(count<GhostDied>(b.events) == 0);
    }
}

TEST_CASE("A Necromite is a small pale worm: out of the ground a bit at a time, flat when it crawls") {
    const glm::vec3 ground{2.0f, 0.0f, 1.0f};
    const auto coming = necromiteWorm(ground, {1.0f, 0.0f, 0.0f}, WormPose::Emerge, 0.5f, 1.0f, 0.3f);
    const auto out = necromiteWorm(ground, {1.0f, 0.0f, 0.0f}, WormPose::Emerge, 1.0f, 1.0f, 0.3f);
    CHECK(coming.front().position.y < out.front().position.y);
    CHECK(out.front().position.y > 0.3f);
    const auto crawling = necromiteWorm(ground, {1.0f, 0.0f, 0.0f}, WormPose::Crawl, 0.0f, 1.0f, 0.3f);
    for (const Strands::Point& p : crawling) {
        CHECK(std::isfinite(p.position.x));
        CHECK(p.position.y < 0.12f);
        CHECK(p.position.y >= 0.0f);
    }
    CHECK(crawling.front().position.x > crawling.back().position.x + 0.3f);
}

TEST_CASE("A zombie is nobody: it revives no one, is not revived, does not mend; and the player it is made of cannot be got up") {
    Trio t;
    REQUIRE(t.roster.downed(0));
    CHECK_FALSE(t.roster.possess(1, kZombie + 1, 1.0f));
    REQUIRE(t.roster.possess(0, kZombie, 0.8f));
    CHECK_FALSE(t.roster.possess(0, kZombie + 4, 1.0f));
    CHECK(t.roster.zombie(kZombie));
    CHECK(t.roster.madeOf(kZombie) == 0);

    t.inputs.push_back({kZombie, {5.0f, 0.0f, 0.0f}, false});
    t.inputs[1] = {1, {0.5f, 0.0f, 0.0f}, true};
    t.run(t.rules.reviveTime + 1.0f);
    CHECK(t.roster.downed(0));
    CHECK(count<PlayerRevived>(t.events) == 0);

    t.roster.hurt(kZombie, 0.3f, glm::vec3(0.0f), t.events);
    t.run(t.rules.regenDelay + 3.0f);
    CHECK(t.roster.find(kZombie)->health == doctest::Approx(0.5f));

    t.roster.hurt(2, 2.0f, glm::vec3(0.0f), t.events);
    t.inputs[1].interact = false;
    t.inputs[3] = {kZombie, {2.2f, 0.0f, 0.0f}, true};
    t.run(t.rules.reviveTime + 1.0f);
    CHECK(t.roster.downed(2));

    t.roster.hurt(kZombie, 2.0f, glm::vec3(0.0f), t.events);
    CHECK(t.roster.downed(kZombie));
    CHECK(t.roster.release(0) == kZombie);
    CHECK(t.roster.find(kZombie) == nullptr);
    t.inputs.pop_back();
    t.inputs[1] = {1, {0.5f, 0.0f, 0.0f}, true};
    t.run(t.rules.reviveTime + 0.5f);
    CHECK_FALSE(t.roster.downed(0));
}

TEST_CASE("With every real player down it is over, zombie or no zombie; one zombie standing does not keep it going") {
    Trio t;
    REQUIRE(t.roster.possess(0, kZombie, 1.0f));
    t.inputs.push_back({kZombie, {5.0f, 0.0f, 0.0f}, false});
    t.roster.hurt(1, 2.0f, glm::vec3(0.0f), t.events);
    t.run(0.5f);
    CHECK(count<PlayerDied>(t.events) == 0);
    t.roster.hurt(2, 2.0f, glm::vec3(0.0f), t.events);
    t.run(0.1f);
    CHECK(count<PlayerDied>(t.events) == 1);
    CHECK(t.roster.find(kZombie) == nullptr);
    CHECK(t.roster.entries().size() == 3);
    for (const RosterEntry& e : t.roster.entries()) {
        CHECK_FALSE(e.downed);
        CHECK(e.possessedBy == kNoPlayer);
    }
}

namespace {
struct Risen {
    Player body{glm::vec3(0.0f)};
    ZombieMind mind;
    ZombieTuning tuning = wormDef().necromite.zombie;
    std::vector<ZombieTarget> targets;
    int live = 0;
    int fired = 0;
    float worstAim = 0.0f;
    bool dived = false;
    bool gotUp = false;
    bool wasLying = false;

    void step() {
        const PlayerState& self = body.state();
        const ZombieOrder order = zombieThink(mind, tuning, self, targets, live, kDt);
        if (order.fire && live > 0) {
            ++fired;
            --live;
            const glm::vec3 eye = self.position + glm::vec3(0.0f, self.eyeHeight, 0.0f);
            const glm::vec3 exact = glm::normalize(targets.front().chest - eye);
            worstAim = std::max(worstAim, std::acos(glm::clamp(glm::dot(exact, order.aim), -1.0f, 1.0f)));
        }
        body.tick(order.command, kDt);
        dived = dived || body.state().stance == Stance::Dive;
        if (body.state().stance == Stance::Crawl) {
            wasLying = true;
        }
        gotUp = gotUp || (wasLying && body.state().stance == Stance::Stand);
    }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            step();
        }
    }
};

}

TEST_CASE("A zombie gets up and empties the gun at them, badly: only the rounds there are, its aim swimming") {
    Risen z;
    z.targets = {{{0.0f, 0.0f, -12.0f}, {0.0f, 1.0f, -12.0f}, true}};
    z.live = 3;
    z.run(z.tuning.riseTime * 0.9f);
    CHECK(z.fired == 0);
    CHECK(glm::length(z.body.state().velocity) < 0.1f);
    z.run(6.0f);
    CHECK(z.fired == 3);
    CHECK(z.worstAim > 0.005f);
    CHECK(z.worstAim < z.tuning.aimWander * 1.3f);

    Risen blind;
    blind.targets = {{{0.0f, 0.0f, -12.0f}, {0.0f, 1.0f, -12.0f}, false}};
    blind.live = 3;
    blind.run(5.0f);
    CHECK(blind.fired == 0);
    CHECK(blind.body.state().position.z < -1.0f);
}

TEST_CASE("With the gun empty a zombie runs at them, jumps and dives, and gets up to do it again") {
    Risen z;
    z.targets = {{{0.0f, 0.0f, -14.0f}, {0.0f, 1.0f, -14.0f}, true}};
    z.live = 0;
    bool rammedInReach = false;
    for (int i = 0; i < static_cast<int>(9.0f / kDt); ++i) {
        z.step();
        const PlayerState& s = z.body.state();
        const float to = glm::length(glm::vec2(s.position.x, s.position.z - (-14.0f)));
        rammedInReach = rammedInReach || (zombieRamming(s, z.tuning) && to < 2.5f);
    }
    CHECK(z.dived);
    CHECK(rammedInReach);
    CHECK(z.gotUp);
    CHECK(z.fired == 0);

    PlayerState walking;
    walking.velocity = {3.0f, 0.0f, 0.0f};
    walking.grounded = true;
    CHECK_FALSE(zombieRamming(walking, z.tuning));
    walking.grounded = false;
    CHECK(zombieRamming(walking, z.tuning));
    walking.velocity = {0.5f, 0.0f, 0.0f};
    CHECK_FALSE(zombieRamming(walking, z.tuning));
}

TEST_CASE("The host can tell a player where their body lies and which of their rounds were fired") {
    const auto placeBytes = net::encodePlace({3.0f, 0.0f, -7.5f});
    net::Reader place(placeBytes);
    CHECK(place.type() == net::Msg::Place);
    CHECK(place.pod<glm::vec3>() == glm::vec3(3.0f, 0.0f, -7.5f));
    CHECK(place.ok());
    const auto spendBytes = net::encodeSpend(4);
    net::Reader spend(spendBytes);
    CHECK(spend.type() == net::Msg::Spend);
    CHECK(spend.pod<std::uint8_t>() == 4);
    CHECK(spend.ok());
}

TEST_CASE("A zombie that walks into something goes round it") {
    Player body{glm::vec3(0.0f)};
    ZombieMind mind;
    const ZombieTuning tuning = wormDef().necromite.zombie;
    const std::vector<ZombieTarget> targets = {{{0.0f, 0.0f, -14.0f}, {0.0f, 1.0f, -14.0f}, false}};

    PlayerEnvironment world;
    world.move = [](const glm::vec3& feet, const glm::vec3& velocity, float dt) {
        const auto inWall = [](const glm::vec3& p) { return std::abs(p.x) < 1.3f && p.z < -3.7f && p.z > -4.8f; };
        ghost::engine::CapsuleMove moved;
        moved.position = feet;
        const glm::vec3 alongX = moved.position + glm::vec3(velocity.x * dt, 0.0f, 0.0f);
        if (!inWall(alongX)) {
            moved.position = alongX;
        }
        const glm::vec3 alongZ = moved.position + glm::vec3(0.0f, 0.0f, velocity.z * dt);
        if (!inWall(alongZ)) {
            moved.position = alongZ;
        }
        moved.position.y = std::max(0.0f, feet.y + velocity.y * dt);
        moved.grounded = moved.position.y <= 0.0f;
        return moved;
    };
    for (int i = 0; i < static_cast<int>(12.0f / kDt); ++i) {
        const ZombieOrder order = zombieThink(mind, tuning, body.state(), targets, 3, kDt);
        body.tick(order.command, kDt, world);
    }
    CHECK(body.state().position.z < -6.0f);
}
