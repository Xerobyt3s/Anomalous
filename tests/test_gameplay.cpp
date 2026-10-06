#include "test.h"

#include "core/arena.h"
#include "math/glm_bridge.h"
#include "sim/sim.h"
#include "world/pickup_body.h"
#include "game/ballistics/ballistics.h"

#include <cmath>
#include <optional>

#include <memory>
#include <variant>
#include <vector>

using namespace anom;

namespace {

constexpr const char* kZoneDir = "assets/zones/testzone";

struct Range {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();
    ghost::game::EventList seen;

    bool setup()
    {
        if (!sim->init(perm, scratch, kZoneDir)) {
            return false;
        }
        Vec3 open = sim->spawn().player_pos;
        const RigidBody* car = sim->phys().body(sim->vehicle().body());
        if (car) {
            Vec3 away = open - car->pos;
            away.y = 0.0f;
            open = car->pos + normalize(away) * 12.0f;
        }
        sim->player(0).init(Vec3{open.x, sim->terrain().heightfield().sample(open.x, open.z) + 0.5f, open.z}, 0.0f);
        run(PlayerCommand{}, 60);
        return true;
    }

    void run(const PlayerCommand& cmd, u32 ticks)
    {
        for (u32 i = 0; i < ticks; i++) {
            PlayerCommand c = cmd;
            c.gameplay = true;
            const Player& p = sim->player(0);
            if (length_sq(c.view_dir) < 1e-6f) {
                c.view_dir = Vec3{0.0f, 0.0f, -1.0f};
            }
            c.view_origin = p.pos() + p.up() * p.movement().state().eye_height;
            sim->tick(c, kFixedDt);
            seen.insert(seen.end(), sim->events().begin(), sim->events().end());
            sim->events().clear();
        }
    }

    Vec3 eye() const
    {
        const Player& p = sim->player(0);
        return p.pos() + p.up() * p.movement().state().eye_height;
    }

    PlayerCommand aim_at(Vec3 target) const
    {
        PlayerCommand c;
        c.gameplay = true;
        c.face_view = true;
        c.view_dir = normalize(target - eye());
        c.barrel_dir = c.view_dir;
        c.muzzle = eye() + c.view_dir * 0.6f;
        return c;
    }

    void draw()
    {
        PlayerCommand c;
        c.holster = true;
        run(c, 1);
        run(PlayerCommand{}, 72);
    }

    void fire(Vec3 target)
    {
        PlayerCommand pull = aim_at(target);
        pull.trigger = true;
        run(pull, 40);
        run(aim_at(target), 20);
    }

    template <typename T>
    u32 count() const
    {
        u32 n = 0;
        for (const ghost::game::GameEvent& e : seen) {
            n += std::holds_alternative<T>(e) ? 1u : 0u;
        }
        return n;
    }

    i32 ghost_type(std::string_view name) const
    {
        const auto& types = sim->gameplay().ghostData().types;
        for (size_t i = 0; i < types.size(); i++) {
            if (types[i].name == name) {
                return static_cast<i32>(i);
            }
        }
        return -1;
    }
};

}

TEST(gameplay, every_player_starts_with_a_loaded_holstered_revolver)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    const PlayerSlot* me = range.sim->slot(0);
    CHECK(me != nullptr);
    if (!me) {
        return;
    }
    CHECK(me->player.movement().state().holstered);
    i32 live = 0;
    for (const auto& chamber : me->gun.mechanism.state().chambers) {
        live += chamber.state == ghost::game::ChamberState::Live ? 1 : 0;
    }
    CHECK(live == ghost::game::kChamberCount);
    CHECK(me->gun.pouch.count(ghost::game::kPlainElement) > 0);
}

TEST(gameplay, a_holstered_gun_does_not_fire_and_a_drawn_one_does)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    const Vec3 ahead = range.eye() + Vec3{0.0f, 0.0f, -20.0f};
    range.fire(ahead);
    CHECK(range.count<ghost::game::ShotFired>() == 0);
    range.draw();
    CHECK(!range.sim->player(0).movement().state().holstered);
    range.fire(ahead);
    CHECK(range.count<ghost::game::ShotFired>() == 1);
    CHECK(range.count<ghost::game::ProjectileImpact>() >= 1);
}

TEST(gameplay, shooting_a_wisp_kills_it_and_it_drops_a_material_you_can_take)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    const i32 wisp = range.ghost_type("wisp");
    CHECK(wisp >= 0);
    range.sim->gameplay().clearWorld();
    PlayerCommand spawn;
    spawn.spawn_ghost = wisp;
    range.run(spawn, 1);
    CHECK(range.sim->gameplay().ghosts().ghosts().size() == 1);
    range.draw();
    for (i32 shot = 0; shot < 12 && range.count<ghost::game::GhostDied>() == 0; shot++) {
        if (range.sim->gameplay().ghosts().ghosts().empty()) {
            break;
        }
        const glm::vec3 at = range.sim->gameplay().ghosts().ghosts().front().position;
        range.fire(from_glm(at));
        range.sim->slot(0)->gun.mechanism.loadAll(ghost::game::Round{});
    }
    u32 ghost_hits = 0;
    for (const ghost::game::GameEvent& e : range.seen) {
        if (const auto* hit = std::get_if<ghost::game::ProjectileImpact>(&e)) {
            ghost_hits += hit->surface == ghost::game::Surface::Ghost ? 1u : 0u;
        }
    }
    CHECK_MSG(ghost_hits > 0, "no round reached the wisp");
    CHECK(range.count<ghost::game::GhostDied>() == 1);
    CHECK(range.count<ghost::game::MaterialDropped>() >= 1);
    range.run(PlayerCommand{}, 240);
    CHECK(!range.sim->gameplay().materialDrops().empty());
    if (range.sim->gameplay().materialDrops().empty()) {
        return;
    }
    const glm::vec3 drop = range.sim->gameplay().materialDrops().front().position;
    range.sim->player(0).init(from_glm(drop) + Vec3{0.4f, 0.2f, 0.0f}, 0.0f);
    range.run(PlayerCommand{}, 30);
    const i32 before = range.sim->gameplay().materials().total();
    PlayerCommand take;
    take.use_down = true;
    range.run(take, 60);
    CHECK(range.sim->gameplay().materials().total() > before);
    CHECK(range.count<ghost::game::MaterialPickedUp>() >= 1);
}

TEST(gameplay, a_wind_round_blows_pickups_away)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    const Vec3 ground = range.sim->player(0).pos() + Vec3{0.0f, 0.0f, -6.0f};
    const Vec3 spot{ground.x, range.sim->terrain().heightfield().sample(ground.x, ground.z) + 0.3f, ground.z};
    const EntityHandle box = interact_spawn_pickup(range.sim->world(), range.sim->phys(), Item{ITEM_BATTERY},
                                                   spot + Vec3{0.6f, 0.0f, 0.0f}, 0.0f, Vec3{});
    range.run(PlayerCommand{}, 120);
    PickupState before;
    pickup_body_state(range.sim->phys(), *range.sim->world().entity(box), before);

    PlayerSlot& me = *range.sim->slot(0);
    const ghost::game::ElementId wind = range.sim->gameplay().ammo().element("wind");
    for (i32 k = 0; k < ghost::game::kChamberCount; k++) {
        me.gun.mechanism.setChamber(k, {ghost::game::ChamberState::Live, ghost::game::Round{wind}});
    }
    range.draw();
    range.fire(spot);
    range.run(PlayerCommand{}, 60);
    PickupState after;
    pickup_body_state(range.sim->phys(), *range.sim->world().entity(box), after);
    CHECK(range.count<ghost::game::GustBurst>() >= 1);
    CHECK(length(after.pos - before.pos) > 0.3f);
}

TEST(gameplay, standing_in_fire_burns)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerSlot& me = *range.sim->slot(0);
    const ghost::game::ElementId fire = range.sim->gameplay().ammo().element("fire");
    for (i32 k = 0; k < ghost::game::kChamberCount; k++) {
        me.gun.mechanism.setChamber(k, {ghost::game::ChamberState::Live, ghost::game::Round{fire}});
    }
    range.draw();
    const Vec3 feet = range.sim->player(0).pos();
    range.fire(feet + Vec3{0.0f, 0.0f, -0.4f});
    range.run(PlayerCommand{}, 120);
    const ghost::game::RosterEntry* entry = range.sim->roster().find(0);
    CHECK(entry != nullptr);
    CHECK(entry && entry->health < 1.0f);
    CHECK(range.count<ghost::game::PlayerDamaged>() >= 1);
}

TEST(gameplay, ejecting_drops_live_rounds_that_can_be_picked_back_up)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    range.draw();
    const i32 pouch = range.sim->slot(0)->gun.pouch.count(ghost::game::kPlainElement);
    PlayerCommand open;
    open.cylinder = true;
    range.run(open, 1);
    range.run(PlayerCommand{}, 60);
    PlayerCommand eject;
    eject.eject = true;
    range.run(eject, 1);
    range.run(PlayerCommand{}, 180);
    CHECK(range.sim->gameplay().dropped().size() == static_cast<size_t>(ghost::game::kChamberCount));
    PlayerCommand take;
    take.use_down = true;
    range.run(take, 600);
    CHECK(range.sim->gameplay().dropped().size() < static_cast<size_t>(ghost::game::kChamberCount));
    CHECK(range.sim->slot(0)->gun.pouch.count(ghost::game::kPlainElement) > pouch);
}

TEST(gameplay, the_shooting_dummy_hurts_you)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerCommand c;
    c.dummy_script = static_cast<i32>(DummyScript::Shoot);
    range.run(c, 1);
    range.run(PlayerCommand{}, 720);
    CHECK(range.count<ghost::game::ShotFired>() >= 2);
    const ghost::game::RosterEntry* entry = range.sim->roster().find(0);
    CHECK(entry && entry->health < 1.0f);
}

TEST(gameplay, the_reloading_dummy_works_its_own_revolver)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerCommand c;
    c.dummy_script = static_cast<i32>(DummyScript::Reload);
    range.run(c, 1);
    bool opened = false;
    for (i32 i = 0; i < 1500; i++) {
        range.run(PlayerCommand{}, 1);
        const PlayerSlot* dummy = range.sim->slot(1);
        opened = opened || (dummy && !dummy->gun.mechanism.state().isClosed());
    }
    CHECK(opened);
    CHECK(range.count<ghost::game::RoundLoaded>() >= 1);
}

TEST(gameplay, a_tailwind_round_hastens_the_one_who_fires_it)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerSlot& me = *range.sim->slot(0);
    const ghost::game::ElementId tailwind = range.sim->gameplay().ammo().element("tailwind");
    for (i32 k = 0; k < ghost::game::kChamberCount; k++) {
        me.gun.mechanism.setChamber(k, {ghost::game::ChamberState::Live, ghost::game::Round{tailwind}});
    }
    range.draw();
    range.fire(range.eye() + Vec3{0.0f, 0.0f, -10.0f});
    CHECK(range.count<ghost::game::SelfCast>() == 1);
    CHECK(range.sim->player(0).movement().state().haste_time > 0.0f);
}

TEST(gameplay, a_world_with_ghosts_and_gunfire_stays_bit_identical)
{
    Range a;
    Range b;
    if (!a.setup() || !b.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerCommand spawn;
    spawn.spawn_ghost = a.ghost_type("poltergeist");
    a.run(spawn, 1);
    b.run(spawn, 1);
    a.draw();
    b.draw();
    const Vec3 target = a.eye() + Vec3{2.0f, 0.0f, -15.0f};
    for (i32 i = 0; i < 4; i++) {
        a.fire(target);
        b.fire(target);
        if (a.sim->checksum() != b.sim->checksum()) {
            FAIL("checksums diverged");
            return;
        }
    }
    a.run(PlayerCommand{}, 240);
    b.run(PlayerCommand{}, 240);
    CHECK(a.sim->checksum() == b.sim->checksum());
}

namespace {

glm::vec3 fly(ghost::game::Ballistics& ballistics, float seconds)
{
    ghost::game::EventList events;
    ballistics.fire(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), ghost::game::Round{}, 0.1f);
    const auto none = [](const glm::vec3&, const glm::vec3&) { return std::optional<ghost::engine::RayHit>{}; };
    for (int i = 0; i < static_cast<int>(seconds * 120.0f); i++) {
        ballistics.tick(1.0f / 120.0f, none, events);
    }
    return ballistics.projectiles().empty() ? glm::vec3(0.0f) : ballistics.projectiles().front().position;
}

}

TEST(gameplay, a_gravity_field_bends_a_bullet_the_way_it_pulls)
{
    ghost::game::Ballistics plain;
    const glm::vec3 down = fly(plain, 1.0f);
    CHECK(down.y < -3.0f);
    CHECK(std::abs(down.x) < 1e-3f);

    ghost::game::Ballistics sideways;
    sideways.setFields([](const glm::vec3&) { return glm::vec3(9.81f, 0.0f, 0.0f); }, {});
    const glm::vec3 side = fly(sideways, 1.0f);
    CHECK(side.x > 3.0f);
    CHECK(std::abs(side.y) < 1e-3f);
}

TEST(gameplay, wind_carries_a_bullet_downwind)
{
    ghost::game::Ballistics still;
    const glm::vec3 calm = fly(still, 1.0f);
    ghost::game::Ballistics windy;
    windy.setFields({}, [](const glm::vec3&) { return glm::vec3(7.0f, 0.0f, 0.0f); });
    const glm::vec3 blown = fly(windy, 1.0f);
    CHECK(blown.x > calm.x + 0.01f);
    CHECK(blown.x < 7.0f);
}

TEST(gameplay, taking_an_item_with_the_gun_out_holsters_first_then_takes_it)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    const Vec3 ahead = range.sim->player(0).pos() + Vec3{0.0f, 0.0f, -1.1f};
    const Vec3 spot{ahead.x, range.sim->terrain().heightfield().sample(ahead.x, ahead.z) + 0.3f, ahead.z};
    const EntityHandle box = interact_spawn_pickup(range.sim->world(), range.sim->phys(), Item{ITEM_OILCAN}, spot, 0.0f, Vec3{});
    range.run(PlayerCommand{}, 90);
    range.draw();
    PlayerSlot& me = *range.sim->slot(0);
    CHECK(!me.player.movement().state().holstered);

    PickupState rest;
    CHECK(pickup_body_state(range.sim->phys(), *range.sim->world().entity(box), rest));
    PlayerCommand press = range.aim_at(rest.pos);
    press.use_pressed = true;
    press.use_down = true;
    range.run(press, 1);
    CHECK(me.interact.hands().kind == ITEM_NONE);
    CHECK(me.player.movement().state().holstered);
    PlayerCommand hold = range.aim_at(rest.pos);
    hold.use_down = true;
    range.run(hold, 90);
    CHECK(me.interact.hands().kind == ITEM_OILCAN);
    CHECK(me.player.movement().state().holster >= 1.0f);
    CHECK(!me.take_pending);
}

TEST(gameplay, drawing_with_an_item_in_hand_sets_it_down_first)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerSlot& me = *range.sim->slot(0);
    me.interact.hands() = Item{ITEM_OILCAN};
    const u32 before = range.sim->world().count();
    PlayerCommand h = range.aim_at(range.eye() + Vec3{0.0f, -0.2f, -1.0f});
    h.holster = true;
    range.run(h, 1);
    CHECK(me.lowering >= 0.0f);
    CHECK(me.player.movement().state().holstered);
    CHECK(me.interact.hands().kind == ITEM_OILCAN);
    range.run(PlayerCommand{}, 120);
    CHECK(me.interact.hands().kind == ITEM_NONE);
    CHECK(range.sim->world().count() == before + 1);
    CHECK(!me.player.movement().state().holstered);
    CHECK(me.player.movement().state().holster < 0.01f);
    CHECK(me.lowering < 0.0f);
}

TEST(gameplay, an_open_cylinder_keeps_the_player_from_aiming_or_sprinting)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    range.draw();
    PlayerCommand open;
    open.cylinder = true;
    range.run(open, 1);
    PlayerCommand hold;
    hold.aim = true;
    hold.run = true;
    hold.move_z = -1.0f;
    range.run(hold, 60);
    const PlayerSlot* me = range.sim->slot(0);
    CHECK(!me->gun.mechanism.state().isClosed());
    CHECK(!me->player.movement().state().aiming);
    CHECK(!me->player.movement().state().sprinting);

    PlayerCommand close = hold;
    close.close_cylinder = true;
    range.run(close, 1);
    range.run(hold, 90);
    CHECK(me->gun.mechanism.state().isClosed());
    hold.run = false;
    range.run(hold, 2);
    CHECK(me->player.movement().state().aiming);
}

TEST(gameplay, an_elemental_round_that_skips_off_the_ground_leaves_its_effect_only_where_it_stops)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    PlayerSlot* me = range.sim->slot(0);
    const ghost::game::ElementId fire = range.sim->gameplay().ammo().element("fire");
    for (int k = 0; k < ghost::game::kChamberCount; k++) {
        me->gun.mechanism.setChamber(k, {ghost::game::ChamberState::Live, ghost::game::Round{fire}});
    }
    range.draw();
    const Heightfield& field = range.sim->terrain().heightfield();
    const Vec3 eye = range.eye();
    Vec3 target{};
    f32 best = 1e9f;
    for (u32 h = 0; h < 32; h++) {
        const f32 yaw = static_cast<f32>(h) * (6.2831853f / 32.0f);
        const Vec3 heading{std::cos(yaw), 0.0f, std::sin(yaw)};
        for (f32 d = 12.0f; d <= 40.0f; d += 2.0f) {
            const Vec3 at = eye + heading * d;
            const f32 ground = field.sample(at.x, at.z);
            const f32 slope = std::atan2(field.sample(at.x + heading.x, at.z + heading.z) - field.sample(at.x - heading.x, at.z - heading.z), 2.0f);
            const f32 side = std::abs(field.sample(at.x - heading.z, at.z + heading.x) - field.sample(at.x + heading.z, at.z - heading.x)) * 0.5f;
            const f32 pitch = std::atan2(ground - eye.y, d);
            const f32 grazing = (slope - pitch) * 57.29578f;
            const f32 miss = std::abs(grazing - 4.0f) + side * 40.0f;
            if (grazing > 1.5f && miss < best) {
                best = miss;
                target = Vec3{at.x, ground, at.z};
            }
        }
    }
    CHECK(best < 3.0f);
    const Vec3 dir = normalize(target - eye);
    const size_t volumes_before = range.sim->gameplay().volumes().all().size();
    range.seen.clear();
    range.fire(eye + dir * 60.0f);
    u32 bounces = 0;
    u32 stops = 0;
    for (const ghost::game::GameEvent& e : range.seen) {
        if (const auto* hit = std::get_if<ghost::game::ProjectileImpact>(&e)) {
            (hit->ricochet ? bounces : stops) += 1;
        }
    }
    u32 fires = 0;
    for (const ghost::game::ElementVolume& v : range.sim->gameplay().volumes().all()) {
        fires += v.element == fire ? 1u : 0u;
    }
    CHECK(bounces >= 1);
    CHECK(stops <= 1);
    CHECK(fires == stops);
    CHECK(range.sim->gameplay().volumes().all().size() - volumes_before == stops);
}

TEST(gameplay, mimics_and_wisps_on_the_islands_keep_to_their_island)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    range.run(PlayerCommand{}, 600);
    u32 mimics = 0;
    u32 wisps = 0;
    for (const ghost::game::Ghost& g : range.sim->gameplay().ghosts().ghosts()) {
        const glm::vec3 up = range.sim->gameplay().upAt(g.position);
        if (up.y > 0.97f) {
            continue;
        }
        const ghost::game::GhostDef& def = range.sim->gameplay().ghosts().def(g);
        CHECK(glm::dot(g.up, up) > 0.95f);
        if (def.behavior == ghost::game::GhostBehavior::Mimic) {
            CHECK(range.sim->physics().raycast(g.position, g.position - up * 1.0f).has_value());
            CHECK(g.state == ghost::game::GhostState::Disguised);
            CHECK(glm::dot(g.surfaceNormal, up) > 0.7f);
            CHECK(glm::distance(g.position, g.home) < 1.0f);
            mimics++;
        } else if (def.behavior == ghost::game::GhostBehavior::Wisp) {
            CHECK(glm::distance(g.position, g.home) < def.wisp.wanderRadius + 2.0f);
            wisps++;
        }
    }
    CHECK(mimics == 4);
    CHECK(wisps == 4);
}

TEST(gameplay, an_inferno_drags_a_wisp_into_its_column_and_burns_it)
{
    Range range;
    if (!range.setup()) {
        FAIL("sim init");
        return;
    }
    range.sim->gameplay().clearWorld();
    PlayerSlot* me = range.sim->slot(0);
    const ghost::game::ElementId inferno = range.sim->gameplay().ammo().element("inferno");
    for (int k = 0; k < ghost::game::kChamberCount; k++) {
        me->gun.mechanism.setChamber(k, {ghost::game::ChamberState::Live, ghost::game::Round{inferno}});
    }
    range.draw();
    const Player& p = range.sim->player(0);
    const Vec3 ahead = frame_forward(p.movement().state().frame, p.movement().state().yaw);
    const Heightfield& field = range.sim->terrain().heightfield();
    const Vec3 near = p.pos() + ahead * 7.0f;
    const Vec3 ground{near.x, field.sample(near.x, near.z), near.z};
    const Vec3 beyond = p.pos() + ahead * 8.5f;
    range.sim->gameplay().spawnGhost("wisp", to_glm(Vec3{beyond.x, field.sample(beyond.x, beyond.z) + 1.2f, beyond.z}));
    CHECK(range.sim->gameplay().ghosts().ghosts().size() == 1);
    if (range.sim->gameplay().ghosts().ghosts().size() != 1) {
        return;
    }
    const f32 full = range.sim->gameplay().ghosts().ghosts().front().health;
    range.seen.clear();
    PlayerCommand pull = range.aim_at(ground);
    pull.trigger = true;
    bool caught = false;
    f32 lowest = full;
    for (u32 t = 0; t < 600; t++) {
        range.run(t < 40 ? pull : range.aim_at(ground), 1);
        const auto& ghosts = range.sim->gameplay().ghosts().ghosts();
        if (ghosts.empty()) {
            break;
        }
        caught = caught || ghosts.front().caught > 0.0f;
        lowest = std::min(lowest, ghosts.front().health);
    }
    CHECK(caught);
    CHECK((lowest < full * 0.7f || range.count<ghost::game::GhostDied>() > 0));
}
