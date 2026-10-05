#include "engine/assets/asset_path.h"
#include "game/ghosts/ghost_world.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <tuple>
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

GhostQuarry playerAt(const glm::vec3& feet) { return {feet, feet + glm::vec3(0.0f, 1.62f, 0.0f), {0.0f, 0.0f, -1.0f}, false}; }

template <typename T>
int count(const EventList& events) {
    int n = 0;
    for (const GameEvent& e : events) {
        n += std::holds_alternative<T>(e) ? 1 : 0;
    }
    return n;
}

struct Box {
    glm::vec3 lo;
    glm::vec3 hi;
};

std::optional<GhostSurfaceHit> castBoxes(const std::vector<Box>& boxes, const glm::vec3& from, const glm::vec3& to) {
    std::optional<GhostSurfaceHit> best;
    float bestT = 2.0f;
    const glm::vec3 d = to - from;
    auto consider = [&](float t, const glm::vec3& normal) {
        if (t >= 0.0f && t <= 1.0f && t < bestT) {
            bestT = t;
            best = GhostSurfaceHit{from + d * t, normal};
        }
    };
    if (from.y >= 0.0f && to.y < 0.0f) {
        consider(from.y / (from.y - to.y), {0.0f, 1.0f, 0.0f});
    }
    for (const Box& box : boxes) {
        float tNear = 0.0f;
        float tFar = 1.0f;
        int axisNear = -1;
        bool miss = false;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(d[a]) < 1e-9f) {
                if (from[a] < box.lo[a] || from[a] > box.hi[a]) {
                    miss = true;
                }
                continue;
            }
            float t0 = (box.lo[a] - from[a]) / d[a];
            float t1 = (box.hi[a] - from[a]) / d[a];
            if (t0 > t1) {
                std::swap(t0, t1);
            }
            if (t0 > tNear) {
                tNear = t0;
                axisNear = a;
            }
            tFar = std::min(tFar, t1);
        }
        if (miss || tNear > tFar || axisNear < 0) {
            continue;
        }
        glm::vec3 normal{0.0f};
        normal[axisNear] = d[axisNear] > 0.0f ? -1.0f : 1.0f;
        consider(tNear, normal);
    }
    return best;
}

struct Scene {
    GhostWorld world{data()};
    std::vector<GhostQuarry> players{playerAt({0.0f, 0.0f, 0.0f})};
    std::vector<Box> boxes;
    GhostContext context;
    EventList events;

    Scene() {
        context.raycast = [this](const glm::vec3& from, const glm::vec3& to) { return castBoxes(boxes, from, to); };
        context.blocked = [this](const glm::vec3& from, const glm::vec3& to) {
            const auto hit = castBoxes(boxes, from, to);
            return hit.has_value() && hit->point.y > 0.01f;
        };
    }
    void run(float seconds) {
        for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
            step();
        }
    }
    void step() {
        context.players = players;
        world.tick(kDt, context, events);
    }
    const Ghost& ghost() const { return world.ghosts().front(); }
};

GhostTypeId typeOf(const char* name) { return data().type(name); }

}

TEST_CASE("Ball lightning waits throwing arcs, then hops in zig-zag bursts of three toward its quarry") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, -18.0f});
    s.world.spawn(typeOf("ball_lightning"), {0.0f, 1.6f, 0.0f});
    const BallLightningParams& p = data().types[typeOf("ball_lightning")].ballLightning;
    s.run(p.chargeTime * 0.9f);
    CHECK(s.ghost().state == GhostState::Charge);
    CHECK(count<BallArcCharged>(s.events) >= 2);
    CHECK(count<BallArc>(s.events) >= 1);
    CHECK(count<BallHopped>(s.events) == 0);
    s.events.clear();
    s.run(p.chargeTime * 0.1f + p.hopEvery * static_cast<float>(p.hops) + 0.1f);
    std::vector<BallHopped> hops;
    for (const GameEvent& e : s.events) {
        if (const auto* h = std::get_if<BallHopped>(&e)) {
            hops.push_back(*h);
        }
    }
    REQUIRE(hops.size() == static_cast<std::size_t>(p.hops));

    const glm::vec3 ahead{0.0f, 0.0f, -1.0f};
    const glm::vec3 side = glm::normalize(glm::cross(ahead, glm::vec3(0.0f, 1.0f, 0.0f)));
    float lastSide = 0.0f;
    for (const BallHopped& h : hops) {
        const glm::vec3 step = h.to - h.from;
        CHECK(glm::length(glm::vec2(step.x, step.z)) > p.hopMin * 0.5f);
        CHECK(glm::dot(step, ahead) > 0.0f);
        const float across = glm::dot(step, side);
        if (lastSide != 0.0f) {
            CHECK(across * lastSide < 0.0f);
        }
        lastSide = across;
    }
    CHECK(s.ghost().position.z < -5.0f);
}

TEST_CASE("Near a player, ball lightning hops round them rather than at them") {
    Scene s;
    s.world.spawn(typeOf("ball_lightning"), {4.0f, 1.6f, 0.0f});
    const BallLightningParams& p = data().types[typeOf("ball_lightning")].ballLightning;
    s.run(p.chargeTime + p.hopEvery * static_cast<float>(p.hops) + 0.1f);
    int hops = 0;
    for (const GameEvent& e : s.events) {
        if (const auto* h = std::get_if<BallHopped>(&e)) {
            ++hops;
            const float distance = glm::length(glm::vec2(h->to.x, h->to.z));
            CHECK(distance > p.circleRadius * 0.7f);
            CHECK(distance < p.circleRadius * 1.3f);
        }
    }
    CHECK(hops == p.hops);
}

TEST_CASE("Ball lightning cannot be hit mid-burst, only while it waits; lightning does nothing, wind hurts it more") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, -30.0f});
    const auto id = s.world.spawn(typeOf("ball_lightning"), {0.0f, 1.6f, 0.0f});
    const BallLightningParams& p = data().types[typeOf("ball_lightning")].ballLightning;
    const glm::vec3 at = s.ghost().position;
    CHECK(s.world.raycast(at + glm::vec3(0.0f, 0.0f, 5.0f), at - glm::vec3(0.0f, 0.0f, 5.0f)).has_value());
    const float full = s.ghost().health;
    CHECK(s.world.damage(id, 1.0f, DamageKind::Lightning, at, s.events));
    CHECK(s.ghost().health == doctest::Approx(full));
    s.run(0.5f);
    s.world.damage(id, 0.2f, DamageKind::Wind, s.ghost().position, s.events);
    const float windLoss = full - s.ghost().health;
    CHECK(windLoss > 0.2f * data().roundDamage * 1.5f);

    s.run(p.flinchTime + data().types[typeOf("ball_lightning")].hitstopMax + 0.03f);
    REQUIRE(s.ghost().state == GhostState::Hop);
    const glm::vec3 now = s.ghost().position;
    CHECK_FALSE(s.world.raycast(now + glm::vec3(0.0f, 0.0f, 5.0f), now - glm::vec3(0.0f, 0.0f, 5.0f)).has_value());
}

TEST_CASE("Ball lightning's arcs hurt whoever is where they strike, and nobody away from it") {
    Scene s;
    s.players = {playerAt({0.0f, 0.0f, -2.0f}), playerAt({40.0f, 0.0f, 0.0f})};
    s.world.spawn(typeOf("ball_lightning"), {0.0f, 1.6f, 0.0f});
    s.run(2.0f);
    for (const GameEvent& e : s.events) {
        if (const auto* hurt = std::get_if<PlayerDamaged>(&e)) {
            CHECK(hurt->player == 0);
            CHECK(glm::distance(hurt->from, glm::vec3(0.0f, 0.0f, -2.0f)) < 2.5f);
        }
    }

    Scene t;
    t.players[0] = playerAt({0.0f, 0.0f, 1.0f});
    t.world.spawn(typeOf("ball_lightning"), {0.0f, 1.6f, 0.0f});
    int hurts = 0;
    for (int round = 0; round < 6; ++round) {
        t.run(2.0f);
        hurts += count<PlayerDamaged>(t.events);
        t.events.clear();
    }
    CHECK(hurts > 0);
}

TEST_CASE("A mimic passes itself off as a material until someone lingers by it, tries to take it, or shoots it") {
    {
        Scene s;
        s.players[0] = playerAt({20.0f, 0.0f, 0.0f});
        s.world.spawn(typeOf("mimic"), {0.0f, 0.25f, 0.0f});
        CHECK(s.ghost().disguise >= 0);
        s.run(5.0f);
        CHECK(s.ghost().state == GhostState::Disguised);
        s.players[0] = playerAt({1.5f, 0.0f, 0.0f});
        s.run(1.0f);
        CHECK(s.ghost().state == GhostState::Disguised);
        s.run(2.0f);
        CHECK(s.ghost().state != GhostState::Disguised);
        CHECK(count<MimicRevealed>(s.events) == 1);
    }
    {
        Scene s;
        s.players[0] = playerAt({1.2f, 0.0f, 0.0f});
        s.players[0].interacting = true;
        s.world.spawn(typeOf("mimic"), {0.0f, 0.25f, 0.0f});
        s.run(0.5f);
        CHECK(s.ghost().state == GhostState::Reveal);
    }
    {
        Scene s;
        s.players[0] = playerAt({20.0f, 0.0f, 0.0f});
        const auto id = s.world.spawn(typeOf("mimic"), {0.0f, 0.25f, 0.0f});
        s.world.damage(id, 0.5f, DamageKind::Plain, {0.0f, 0.25f, 0.0f}, s.events);
        s.run(0.2f);
        CHECK(s.ghost().state == GhostState::Reveal);
    }
}

TEST_CASE("A found-out mimic climbs a wall by its quarry, and drops on them from above") {
    Scene s;

    s.boxes.push_back({{1.0f, 0.0f, -3.0f}, {1.5f, 5.0f, 3.0f}});
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    const auto id = s.world.spawn(typeOf("mimic"), {-6.0f, 0.25f, 0.0f});
    s.world.damage(id, 0.1f, DamageKind::Plain, {-6.0f, 0.25f, 0.0f}, s.events);
    bool climbed = false;
    bool dropped = false;
    float highest = 0.0f;
    for (int i = 0; i < static_cast<int>(12.0f / kDt) && !dropped; ++i) {
        s.step();
        const Ghost& g = s.ghost();
        if (g.attached && g.surfaceNormal.x < -0.9f) {
            climbed = true;
            highest = std::max(highest, g.position.y);
            CHECK(std::abs(g.position.x - (1.0f - data().types[typeOf("mimic")].mimic.bodyRadius)) < 0.05f);
        }
        dropped = climbed && g.state == GhostState::Leap;
    }
    CHECK(climbed);
    CHECK(highest > 1.3f);
    CHECK(dropped);
}

TEST_CASE("A mimic winds up and lashes whoever is still in reach, then cools down") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    const auto id = s.world.spawn(typeOf("mimic"), {1.0f, 0.25f, 0.0f});
    s.world.damage(id, 0.1f, DamageKind::Plain, {1.0f, 0.25f, 0.0f}, s.events);
    const MimicParams& p = data().types[typeOf("mimic")].mimic;
    s.run(p.revealTime + 0.3f);
    s.run(p.thrashWindup + 0.6f);
    CHECK(count<MimicThrash>(s.events) >= 1);
    int hits = 0;
    for (const GameEvent& e : s.events) {
        if (const auto* hurt = std::get_if<PlayerDamaged>(&e); hurt && hurt->amount != doctest::Approx(p.revealDamage)) {
            ++hits;
            CHECK(hurt->amount == doctest::Approx(p.thrashDamage));
            CHECK(glm::length(hurt->shove) > 1.0f);
        }
    }
    CHECK(hits == 1);
}

TEST_CASE("A killed mimic leaves Effigy Thread, or now and then what it was pretending to be") {
    const GhostDef& def = data().types[typeOf("mimic")];
    const auto thread = ammo().findMaterial("effigy_thread");
    REQUIRE(thread);
    const auto usual = chooseDrop(def, 3, 0.5f, 0.9f);
    REQUIRE(usual);
    CHECK(usual->material == *thread);
    const auto lucky = chooseDrop(def, 3, 0.5f, def.dropDisguiseChance * 0.5f);
    REQUIRE(lucky);
    CHECK(lucky->material == 3);
    const GhostDef& ball = data().types[typeOf("ball_lightning")];
    CHECK(chooseDrop(ball, -1, 0.5f, 0.0f)->material == *ammo().findMaterial("storm_glass"));
}

namespace {
std::uint32_t huntingMimic(Scene& s, const glm::vec3& at) {
    const auto id = s.world.spawn(typeOf("mimic"), at);
    s.world.damage(id, 0.05f, DamageKind::Plain, at, s.events);
    const GhostDef& def = data().types[typeOf("mimic")];
    s.run(def.mimic.revealTime + def.hitstopMax + 0.05f);
    s.events.clear();
    return id;
}

bool inView(const GhostQuarry& player, const glm::vec3& at) {
    return glm::dot(glm::normalize(at - player.eye), player.viewDirection) > 0.5f;
}

}

TEST_CASE("A hunting mimic does not charge someone watching it: it works round behind them and only stops where unseen") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    huntingMimic(s, {0.0f, 0.25f, -6.0f});
    bool gotBehind = false;
    bool attacked = false;
    for (int i = 0; i < static_cast<int>(8.0f / kDt) && !attacked; ++i) {
        s.step();
        const Ghost& g = s.ghost();
        attacked = g.state == GhostState::Leap || g.state == GhostState::Windup;
        if (attacked) {
            break;
        }
        const glm::vec3 offset = g.position - s.players[0].feet;
        gotBehind = gotBehind || offset.z > 1.0f;
        if (g.state == GhostState::Pause) {
            CHECK_FALSE(inView(s.players[0], g.position));
        }
        if (inView(s.players[0], g.position)) {
            CHECK(glm::length(glm::vec2(offset.x, offset.z)) > 1.9f);
        }
    }
    CHECK(gotBehind);
    CHECK(attacked);
}

TEST_CASE("With a pillar to hand a mimic climbs the side its quarry can't see, and comes down on them from the top") {
    Scene s;
    s.boxes.push_back({{2.0f, 0.0f, -0.5f}, {3.0f, 4.0f, 0.5f}});
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    s.players[0].viewDirection = {1.0f, 0.0f, 0.0f};
    huntingMimic(s, {8.0f, 0.25f, 3.0f});
    const MimicParams& p = data().types[typeOf("mimic")].mimic;
    bool climbedFarSide = false;
    bool leapt = false;
    float leaptFrom = 0.0f;
    for (int i = 0; i < static_cast<int>(20.0f / kDt) && !leapt; ++i) {
        const float before = s.ghost().position.y;
        s.step();
        const Ghost& g = s.ghost();
        climbedFarSide = climbedFarSide || (g.attached && g.surfaceNormal.x > 0.9f && g.position.y > 1.0f);
        if (g.state == GhostState::Leap) {
            leapt = true;
            leaptFrom = before;
        }
    }
    CHECK(climbedFarSide);
    REQUIRE(leapt);
    CHECK(leaptFrom > 1.3f);
    s.events.clear();
    s.run(1.5f);
    int leapHits = 0;
    for (const GameEvent& e : s.events) {
        if (const auto* hurt = std::get_if<PlayerDamaged>(&e); hurt && hurt->amount == doctest::Approx(p.leapDamage)) {
            ++leapHits;
            CHECK(glm::length(hurt->shove) > 1.0f);
        }
    }
    CHECK(leapHits == 1);
}

TEST_CASE("A hunting mimic slips some of the shots it sees coming; one lying disguised never moves") {
    {
        Scene s;
        s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
        s.players[0].viewDirection = {0.0f, 0.0f, 1.0f};
        const auto id = huntingMimic(s, {0.0f, 0.25f, -9.0f});
        int dodges = 0;
        for (int shot = 0; shot < 40; ++shot) {
            s.run(0.4f);
            const Ghost* g = s.world.find(id);
            REQUIRE(g != nullptr);
            if (!g->perceives || !g->attached) {
                continue;
            }
            const glm::vec3 from = s.players[0].eye;
            s.events.clear();
            s.world.reactToShot(from, glm::normalize(g->position - from), true, s.events);
            if (count<GhostDodged>(s.events) > 0) {
                ++dodges;
                CHECK_FALSE(s.world.raycast(from, g->position + glm::normalize(g->position - from)).has_value());
            }
        }
        CHECK(dodges > 0);
        CHECK(dodges < 40);
    }
    {
        Scene s;
        s.players[0] = playerAt({0.0f, 0.0f, 8.0f});
        s.world.spawn(typeOf("mimic"), {0.0f, 0.25f, 0.0f});
        s.run(0.5f);
        for (int shot = 0; shot < 30; ++shot) {
            s.world.reactToShot(s.players[0].eye, glm::normalize(s.ghost().position - s.players[0].eye), true, s.events);
        }
        CHECK(count<GhostDodged>(s.events) == 0);
        CHECK(s.ghost().state == GhostState::Disguised);
    }
}

TEST_CASE("A mimic that loses everyone runs off, hides and is a harmless-looking thing again, until someone comes by") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    huntingMimic(s, {0.0f, 0.25f, -8.0f});
    const glm::vec3 was = s.ghost().position;
    s.players[0].hidden = true;
    s.run(6.0f);
    CHECK(s.ghost().state == GhostState::Disguised);
    CHECK(s.ghost().disguise >= 0);
    CHECK(count<MimicConcealed>(s.events) == 1);
    CHECK(glm::distance(s.ghost().position, was) > 5.0f);
    CHECK(s.ghost().surfaceNormal.y > 0.9f);

    s.players[0] = playerAt(s.ghost().position + glm::vec3(1.2f, -0.25f, 0.0f));
    s.run(3.0f);
    CHECK(s.ghost().state != GhostState::Disguised);
    CHECK(count<MimicRevealed>(s.events) >= 1);
}

TEST_CASE("A mimic may pass itself off as a small box; killed, that one leaves only Effigy Thread") {
    const GhostDef& def = data().types[typeOf("mimic")];
    CHECK(pickDisguise(def, def.mimic.boxChance * 0.5f, 0.3f) == kDisguiseBox);
    const std::int16_t material = pickDisguise(def, 0.99f, 0.0f);
    CHECK(material >= 0);
    CHECK(material < kDisguiseBox);
    const auto drop = chooseDrop(def, kDisguiseBox, 0.5f, 0.0f);
    REQUIRE(drop);
    CHECK(drop->material == *ammo().findMaterial("effigy_thread"));
}

TEST_CASE("A mimic crosses a low block in its way (up one side, over, down the other) instead of getting stuck on top of it") {
    Scene s;
    s.boxes.push_back({{-0.5f, 0.0f, -3.5f}, {0.5f, 0.9f, -2.5f}});
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    s.players[0].viewDirection = {0.0f, 0.0f, 1.0f};
    huntingMimic(s, {0.0f, 0.25f, -8.0f});
    bool attacked = false;
    int onBlock = 0;
    for (int i = 0; i < static_cast<int>(10.0f / kDt) && !attacked; ++i) {
        s.step();
        const Ghost& g = s.ghost();
        attacked = g.state == GhostState::Leap || g.state == GhostState::Windup;
        const bool overBlock = std::abs(g.position.x) < 0.8f && g.position.z > -3.8f && g.position.z < -2.2f && g.position.y > 0.3f;
        onBlock += overBlock && g.state == GhostState::Hunt ? 1 : 0;
    }
    CHECK(attacked);
    CHECK(onBlock < static_cast<int>(1.5f / kDt));
}

TEST_CASE("Ball lightning floats slowly about, quiet, until it notices someone; waiting between bursts it only drifts") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, -60.0f});
    s.world.spawn(typeOf("ball_lightning"), {0.0f, 1.6f, 0.0f});
    const BallLightningParams& p = data().types[typeOf("ball_lightning")].ballLightning;
    const glm::vec3 start = s.ghost().position;
    float fastest = 0.0f;
    for (int i = 0; i < static_cast<int>(12.0f / kDt); ++i) {
        s.step();
        fastest = std::max(fastest, glm::length(glm::vec2(s.ghost().velocity.x, s.ghost().velocity.z)));
    }
    CHECK(s.ghost().state == GhostState::Wander);
    CHECK(count<BallArcCharged>(s.events) == 0);
    CHECK(count<BallHopped>(s.events) == 0);
    CHECK(fastest <= p.wanderSpeed + 0.05f);
    CHECK(glm::distance(s.ghost().position, start) > 0.5f);
    CHECK(glm::distance(s.ghost().position, start) < p.wanderRadius + 1.0f);

    s.players[0] = playerAt(s.ghost().position + glm::vec3(0.0f, -1.6f, -12.0f));
    s.step();
    const glm::vec3 waitingFrom = s.ghost().position;
    s.run(p.chargeTime * 0.9f);
    CHECK(s.ghost().state == GhostState::Charge);
    CHECK(count<BallArcCharged>(s.events) >= 2);
    const float drifted = glm::length(glm::vec2(s.ghost().position.x - waitingFrom.x, s.ghost().position.z - waitingFrom.z));
    CHECK(drifted > 0.1f);
    CHECK(drifted < p.driftSpeed * p.chargeTime + 0.4f);
}

TEST_CASE("A mimic is hit round its raised core once it is out, and only as big as its disguise before") {
    Scene s;
    s.players[0] = playerAt({20.0f, 0.0f, 0.0f});
    const auto id = s.world.spawn(typeOf("mimic"), {0.0f, 0.25f, 0.0f});
    const GhostDef& def = data().types[typeOf("mimic")];
    s.run(0.2f);

    CHECK_FALSE(s.world.raycast({-3.0f, 0.25f + def.mimic.disguisedRadius + 0.1f, 0.0f}, {3.0f, 0.25f + def.mimic.disguisedRadius + 0.1f, 0.0f}).has_value());
    CHECK(s.world.raycast({-3.0f, 0.25f, 0.0f}, {3.0f, 0.25f, 0.0f}).has_value());
    s.world.damage(id, 0.05f, DamageKind::Plain, {0.0f, 0.25f, 0.0f}, s.events);
    s.run(0.3f);
    REQUIRE(s.ghost().state == GhostState::Reveal);

    const float core = 0.25f + def.mimic.standHeight;
    CHECK(s.world.raycast({-3.0f, core + def.radius - 0.05f, 0.0f}, {3.0f, core + def.radius - 0.05f, 0.0f}).has_value());
    CHECK_FALSE(s.world.raycast({-3.0f, core + def.radius + 0.1f, 0.0f}, {3.0f, core + def.radius + 0.1f, 0.0f}).has_value());
}

TEST_CASE("A mimic thrashing out of its disguise strikes whoever is right by it, once, and nobody further off") {
    const MimicParams& p = data().types[typeOf("mimic")].mimic;
    Scene s;
    s.players = {playerAt({0.9f, 0.0f, 0.0f}), playerAt({3.0f, 0.0f, 0.0f})};
    s.players[0].interacting = true;
    s.world.spawn(typeOf("mimic"), {0.0f, 0.25f, 0.0f});
    s.run(p.revealGrab + p.revealTime * 0.9f);
    REQUIRE(s.ghost().state == GhostState::Reveal);
    int hits = 0;
    for (const GameEvent& e : s.events) {
        if (const auto* hurt = std::get_if<PlayerDamaged>(&e)) {
            ++hits;
            CHECK(hurt->player == 0);
            CHECK(hurt->amount == doctest::Approx(p.revealDamage));
            CHECK(hurt->shove.x > 1.0f);
        }
    }
    CHECK(hits == 1);
}

TEST_CASE("A mimic has weight: it gathers pace from a standstill and swings its heading round rather than turning on the spot") {
    const MimicParams& p = data().types[typeOf("mimic")].mimic;
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    huntingMimic(s, {0.0f, 0.25f, -6.0f});
    glm::vec3 before = s.ghost().velocity;
    GhostState stateBefore = s.ghost().state;
    float sharpest = 0.0f;
    float biggestJump = 0.0f;
    int measured = 0;
    for (int i = 0; i < static_cast<int>(6.0f / kDt); ++i) {
        s.step();
        const Ghost& g = s.ghost();
        const bool running = g.state == GhostState::Hunt && stateBefore == GhostState::Hunt && g.attached && g.dodging <= 0.0f;
        if (running) {
            biggestJump = std::max(biggestJump, glm::length(g.velocity) - glm::length(before));
            if (glm::length(g.velocity) > 1.0f && glm::length(before) > 1.0f) {
                sharpest = std::max(sharpest, std::acos(glm::clamp(glm::dot(glm::normalize(g.velocity), glm::normalize(before)), -1.0f, 1.0f)));
                ++measured;
            }
        }
        before = g.velocity;
        stateBefore = g.state;
    }
    CHECK(measured > 50);
    CHECK(sharpest <= p.turnRate * kDt * 1.2f);
    CHECK(biggestJump <= p.acceleration * kDt * 1.2f);
}

namespace {
const GhostDef& krakaDef() { return data().types[typeOf("vasskraka")]; }

std::uint32_t circlingKraka(Scene& s, const glm::vec3& at) {
    const auto id = s.world.spawn(typeOf("vasskraka"), at);
    for (int i = 0; i < 600 && s.world.find(id)->state != GhostState::Circle; ++i) {
        s.step();
    }
    s.events.clear();
    return id;
}

template <typename Predicate>
bool runUntil(Scene& s, float seconds, Predicate done) {
    for (int i = 0; i < static_cast<int>(seconds / kDt); ++i) {
        s.step();
        if (done()) {
            return true;
        }
    }
    return false;
}

}

TEST_CASE("A Vasskraka roosts on the nearest wall and stays there until it notices someone; then it takes to the air and circles them") {
    Scene s;
    s.boxes.push_back({{4.0f, 0.0f, -3.0f}, {5.0f, 6.0f, 3.0f}});
    s.players[0] = playerAt({0.0f, 0.0f, 60.0f});
    s.world.spawn(typeOf("vasskraka"), {1.0f, 2.0f, 0.0f});
    s.run(4.0f);
    CHECK(s.ghost().state == GhostState::Roost);
    CHECK(s.ghost().attached);
    CHECK(s.ghost().position.x > 3.5f);
    CHECK(s.ghost().surfaceNormal.x < -0.9f);
    const glm::vec3 roost = s.ghost().position;
    s.run(2.0f);
    CHECK(glm::distance(s.ghost().position, roost) < 0.01f);

    const VasskrakaParams& p = krakaDef().vasskraka;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    REQUIRE(runUntil(s, 3.0f, [&] { return s.ghost().state == GhostState::Circle; }));
    CHECK(count<KrakaScattered>(s.events) >= 1);

    float turned = 0.0f;
    float last = std::atan2(s.ghost().position.z, s.ghost().position.x);
    bool inPlace = false;
    for (int i = 0; i < static_cast<int>(2.0f / kDt) && s.ghost().state == GhostState::Circle; ++i) {
        s.step();
        const glm::vec3 at = s.ghost().position;
        float step = std::atan2(at.z, at.x) - last;
        step -= 6.2831853f * std::round(step / 6.2831853f);
        turned += step;
        last = std::atan2(at.z, at.x);
        const float out = glm::length(glm::vec2(at.x, at.z));
        inPlace = inPlace || (std::abs(out - p.circleRadius) < 1.2f && std::abs(at.y - p.circleHeight) < 1.0f);
    }
    CHECK(inPlace);
    CHECK(std::abs(turned) > 0.5f);
}

TEST_CASE("A Vasskraka stoops through its quarry, striking once, harder the bigger it is") {
    const VasskrakaParams& p = krakaDef().vasskraka;
    for (const float share : {1.0f, 0.5f}) {
        Scene s;
        s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
        const auto id = circlingKraka(s, {5.0f, 2.5f, 0.0f});
        std::vector<Ghost> all = s.world.ghosts();
        all[0].health = krakaDef().health * share;
        s.world.replace(all);
        float diveHits = 0.0f;
        int dives = 0;

        for (int i = 0; i < static_cast<int>(40.0f / kDt) && dives == 0; ++i) {
            const GhostState before = s.world.find(id)->state;
            if (before != GhostState::Stoop) {
                s.events.clear();
            }
            s.step();
            const Ghost* g = s.world.find(id);
            REQUIRE(g != nullptr);
            if (before == GhostState::Stoop && g->state != GhostState::Stoop) {
                ++dives;
            }
        }
        REQUIRE(dives == 1);
        int hits = 0;
        for (const GameEvent& e : s.events) {
            if (const auto* hurt = std::get_if<PlayerDamaged>(&e)) {
                ++hits;
                diveHits = hurt->amount;
                CHECK(glm::length(hurt->shove) > 1.0f);
            }
        }
        CHECK(hits == 1);
        CHECK(diveHits == doctest::Approx(p.diveDamage * share).epsilon(0.05));
    }
}

TEST_CASE("A Vasskraka's ball drops and bursts on those in the open, not on those behind cover, and it gathers itself again") {
    const VasskrakaParams& p = krakaDef().vasskraka;
    Scene s;
    s.boxes.push_back({{1.0f, 0.0f, -4.0f}, {1.4f, 4.0f, 4.0f}});
    s.players = {playerAt({0.0f, 0.0f, 0.0f}), playerAt({2.2f, 0.0f, 0.0f})};
    const auto id = circlingKraka(s, {-5.0f, 2.5f, 0.0f});
    bool burst = false;
    for (int i = 0; i < static_cast<int>(60.0f / kDt) && !burst; ++i) {
        s.events.clear();
        s.step();
        burst = count<KrakaBurst>(s.events) > 0;
    }
    REQUIRE(burst);
    REQUIRE(s.world.find(id) != nullptr);
    CHECK(s.world.find(id)->state == GhostState::Reform);
    int struck = 0;
    for (const GameEvent& e : s.events) {
        if (const auto* hurt = std::get_if<PlayerDamaged>(&e)) {
            ++struck;
            const GhostQuarry& who = s.players[static_cast<std::size_t>(hurt->player)];
            CHECK(glm::length(glm::vec2(hurt->from.x - who.feet.x, hurt->from.z - who.feet.z)) < p.burstRadius);
            CHECK(hurt->amount <= p.burstDamage * 1.01f);
            CHECK_FALSE(s.context.blocked(hurt->from, glm::mix(who.feet, who.eye, 0.6f)));
        }
    }
    CHECK(struck >= 1);
    CHECK(struck <= 1);
    REQUIRE(runUntil(s, 3.0f, [&] { return s.world.find(id) && s.world.find(id)->state == GhostState::Circle; }));
}

TEST_CASE("A Vasskraka slips shots by scattering into a cloud nothing can hit, and is smaller the less health it has") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    const auto id = circlingKraka(s, {5.0f, 2.5f, 0.0f});
    int dodges = 0;
    for (int shot = 0; shot < 30; ++shot) {
        runUntil(s, 6.0f, [&] { return s.world.find(id)->state == GhostState::Circle && s.world.find(id)->perceives; });
        const Ghost* g = s.world.find(id);
        const glm::vec3 from = s.players[0].eye;
        s.events.clear();
        s.world.reactToShot(from, glm::normalize(g->position - from), true, s.events);
        if (count<GhostDodged>(s.events) > 0) {
            ++dodges;
            s.step();
            CHECK(s.world.find(id)->state == GhostState::Scatter);
            CHECK_FALSE(s.world.raycast(from, s.world.find(id)->position).has_value());
        }
    }
    CHECK(dodges > 3);
    CHECK(dodges < 30);

    Ghost whole = *s.world.find(id);
    whole.state = GhostState::Circle;
    whole.health = krakaDef().health;
    Ghost hurt = whole;
    hurt.health = krakaDef().health * 0.4f;
    CHECK(ghostHitSphere(hurt, krakaDef()).radius < ghostHitSphere(whole, krakaDef()).radius * 0.85f);
    CHECK(vasskrakaSize(hurt, krakaDef()) == doctest::Approx(0.4f));
}

TEST_CASE("Two hurt Vasskrakas swirl together into one, their health added but never past one and a half times a whole one") {
    for (const auto& [first, second, expected] : {std::tuple{0.5f, 0.3f, 0.8f}, std::tuple{0.9f, 0.9f, 1.5f}}) {
        Scene s;
        s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
        s.world.spawn(typeOf("vasskraka"), {5.0f, 2.5f, 0.0f});
        s.world.spawn(typeOf("vasskraka"), {-5.0f, 2.5f, 0.0f});
        runUntil(s, 5.0f, [&] { return s.world.ghosts()[0].state == GhostState::Circle && s.world.ghosts()[1].state == GhostState::Circle; });
        std::vector<Ghost> all = s.world.ghosts();
        REQUIRE(all.size() == 2);
        all[0].health = krakaDef().health * first;
        all[1].health = krakaDef().health * second;
        all[0].timer = all[1].timer = 100.0f;
        s.world.replace(all);
        REQUIRE(runUntil(s, 12.0f, [&] { return s.world.ghosts().size() == 1; }));
        CHECK(count<KrakaMerged>(s.events) == 1);
        CHECK(count<GhostDied>(s.events) == 0);
        CHECK(s.world.ghosts()[0].health == doctest::Approx(krakaDef().health * expected));
    }

    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    s.world.spawn(typeOf("vasskraka"), {5.0f, 2.5f, 0.0f});
    s.world.spawn(typeOf("vasskraka"), {-5.0f, 2.5f, 0.0f});
    std::vector<Ghost> all = s.world.ghosts();
    all[0].timer = all[1].timer = 100.0f;
    s.world.replace(all);
    s.run(6.0f);
    CHECK(s.world.ghosts().size() == 2);
}

TEST_CASE("A Vasskraka divides when someone other than its quarry strikes it with enough of it left: a half for each of them") {
    const VasskrakaParams& p = krakaDef().vasskraka;
    Scene s;
    s.players = {playerAt({0.0f, 0.0f, 0.0f}), playerAt({30.0f, 0.0f, 0.0f})};
    s.players[0].id = 0;
    s.players[1].id = 1;
    const auto id = circlingKraka(s, {-3.0f, 2.5f, 0.0f});
    REQUIRE(s.world.find(id)->quarryId == 0);

    s.world.damage(id, 0.1f, DamageKind::Plain, s.world.find(id)->position, s.events, 0, 0);
    CHECK(s.world.ghosts().size() == 1);
    REQUIRE(runUntil(s, 3.0f, [&] { return s.world.find(id)->state == GhostState::Circle && s.world.find(id)->quarryId == 0; }));

    const float whole = s.world.find(id)->health;
    s.world.damage(id, 0.1f, DamageKind::Plain, s.world.find(id)->position, s.events, 0, 1);
    REQUIRE(s.world.ghosts().size() == 2);
    CHECK(count<KrakaSplit>(s.events) == 1);
    const Ghost kept = s.world.ghosts()[0];
    const Ghost half = s.world.ghosts()[1];
    CHECK(kept.health == doctest::Approx(half.health));
    CHECK(kept.health + half.health == doctest::Approx(whole - 0.1f * data().roundDamage * krakaDef().damage[0]));
    CHECK(half.prefers == 1);

    s.world.damage(kept.id, 0.01f, DamageKind::Plain, kept.position, s.events, 0, 1);
    CHECK(s.world.ghosts().size() == 2);

    Scene t;
    t.players = s.players;
    const auto small = circlingKraka(t, {-3.0f, 2.5f, 0.0f});
    std::vector<Ghost> all = t.world.ghosts();
    all[0].health = krakaDef().health * (p.splitAbove - 0.1f);
    t.world.replace(all);
    t.world.damage(small, 0.01f, DamageKind::Plain, all[0].position, t.events, 0, 1);
    CHECK(t.world.ghosts().size() == 1);

    s.players[1] = playerAt({6.0f, 0.0f, 0.0f});
    s.players[1].id = 1;
    bool wentForThem = false;
    for (int i = 0; i < static_cast<int>(4.0f / kDt); ++i) {
        s.step();
        const Ghost* now = s.world.find(half.id);
        wentForThem = wentForThem || (now && now->quarryId == 1);
    }
    CHECK(wentForThem);
}

TEST_CASE("A gust knocks shards out of a Vasskraka but never the last of it; wind rounds hurt it most and it leaves nothing") {
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 60.0f});
    const auto id = s.world.spawn(typeOf("vasskraka"), {0.0f, 2.0f, 0.0f});
    const float before = s.world.find(id)->health;
    s.world.push({0.0f, 1.0f, 0.0f}, 5.0f, 10.0f);
    CHECK(s.world.find(id)->health < before - 5.0f);
    for (int i = 0; i < 50; ++i) {
        s.world.push({0.0f, 1.0f, 0.0f}, 50.0f, 40.0f);
    }
    CHECK(s.world.find(id)->health >= 1.0f);
    CHECK(krakaDef().damage[static_cast<std::size_t>(DamageKind::Wind)] > krakaDef().damage[static_cast<std::size_t>(DamageKind::Plain)] * 3.0f);
    CHECK_FALSE(chooseDrop(krakaDef(), -1, 0.5f, 0.5f).has_value());
}

TEST_CASE("A Vasskraka flies fast to reach its quarry and slows to circle them, well out; it never uses the ball twice running") {
    const VasskrakaParams& p = krakaDef().vasskraka;
    Scene s;
    s.players[0] = playerAt({0.0f, 0.0f, 0.0f});
    const auto id = s.world.spawn(typeOf("vasskraka"), {0.0f, 3.0f, -19.0f});
    float fastest = 0.0f;
    REQUIRE(runUntil(s, 6.0f, [&] {
        const Ghost* g = s.world.find(id);
        if (g->state == GhostState::Circle) {
            fastest = std::max(fastest, glm::length(g->velocity));
        }
        return g->state == GhostState::Circle && std::abs(glm::length(glm::vec2(g->position.x, g->position.z)) - p.circleRadius) < 1.0f;
    }));
    CHECK(fastest > p.circleSpeed * 1.6f);

    std::vector<Ghost> all = s.world.ghosts();
    all[0].timer = 100.0f;
    s.world.replace(all);
    s.run(1.5f);
    float circling = 0.0f;
    for (int i = 0; i < static_cast<int>(2.0f / kDt); ++i) {
        s.step();
        const Ghost* g = s.world.find(id);
        circling = std::max(circling, glm::length(g->velocity));
        CHECK(std::abs(glm::length(glm::vec2(g->position.x, g->position.z)) - p.circleRadius) < 1.6f);
    }
    CHECK(circling < p.circleSpeed + 1.0f);
    CHECK(p.circleRadius >= 7.0f);

    all = s.world.ghosts();
    all[0].timer = 0.0f;
    all[0].health = krakaDef().health;
    s.world.replace(all);
    s.events.clear();
    std::vector<int> attacks;
    for (int i = 0; i < static_cast<int>(240.0f / kDt); ++i) {
        s.step();
        for (const GameEvent& e : s.events) {
            if (std::holds_alternative<KrakaDive>(e)) {
                attacks.push_back(0);
            } else if (std::holds_alternative<KrakaBall>(e)) {
                attacks.push_back(1);
            }
        }
        s.events.clear();
    }
    REQUIRE(attacks.size() > 20);
    int balls = 0;
    for (std::size_t i = 0; i < attacks.size(); ++i) {
        balls += attacks[i];
        if (i > 0) {
            CHECK_FALSE((attacks[i] == 1 && attacks[i - 1] == 1));
        }
    }
    CHECK(balls > 0);
    CHECK(balls < static_cast<int>(attacks.size()) / 3);
}
