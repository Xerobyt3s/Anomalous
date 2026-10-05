#include "test.h"

#include "core/arena.h"
#include "math/glm_bridge.h"
#include "physics/heightfield.h"
#include "engine/physics/physics_world.h"
#include "math/glm_bridge.h"
#include "physics/world.h"
#include "player/movement.h"
#include "sim/sim.h"

#include <memory>
#include <variant>
#include <vector>

using namespace anom;

namespace {
constexpr f32 kDt = 1.0f / 120.0f;

struct PairRig {
    Arena arena{megabytes(32)};
    Heightfield hf;
    PhysWorld world;
    ghost::engine::PhysicsWorld jolt;
    Movement a;
    Movement b;

    void setup(Vec3 a_feet, Vec3 b_feet)
    {
        hf.init_procedural(arena, 96, 1.0f, 5u, 0.0f);
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        a.set_character(0);
        b.set_character(1);
        a.init(&jolt, a_feet, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
        b.init(&jolt, b_feet, Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    }

    void run(const MoveCommand& ca, const MoveCommand& cb, i32 ticks)
    {
        for (i32 i = 0; i < ticks; i++) {
            a.tick(ca, nullptr, kDt);
            b.tick(cb, nullptr, kDt);
        }
    }

    f32 apart() const
    {
        const Vec3 d = a.state().pos - b.state().pos;
        return length(Vec3{d.x, 0.0f, d.z});
    }
};

MoveCommand walk(Vec2 dir)
{
    MoveCommand cmd;
    cmd.move = dir;
    return cmd;
}

struct SimRig {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();

    bool setup() { return sim->init(perm, scratch, "assets/zones/testzone"); }

    void tick(std::span<const SlotCommand> commands, u32 ticks)
    {
        for (u32 i = 0; i < ticks; i++) {
            sim->tick(commands, kFixedDt);
            sim->events().clear();
        }
    }

    PlayerCommand look_at(PlayerId id, Vec3 target) const
    {
        const Player& p = sim->player(id);
        PlayerCommand cmd;
        cmd.gameplay = true;
        cmd.view_origin = p.pos() + p.up() * kPlayerEyeHeight;
        cmd.view_dir = normalize(target - cmd.view_origin);
        return cmd;
    }
};

bool has_event_downed(const ghost::game::EventList& events)
{
    for (const ghost::game::GameEvent& e : events) {
        if (std::holds_alternative<ghost::game::PlayerDowned>(e)) {
            return true;
        }
    }
    return false;
}

}

TEST(characters, each_slot_gets_its_own_jolt_character)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    CHECK(rig.jolt.characterValid(0));
    CHECK(rig.jolt.characterValid(1));
    CHECK(!rig.jolt.characterValid(2));
    rig.run(walk(Vec2{0.0f, 1.0f}), MoveCommand{}, 60);
    CHECK(rig.a.state().pos.z < -1.0f);
    CHECK_NEAR(rig.b.state().pos.z, 0.0f, 0.05);
}

TEST(characters, two_players_walking_into_each_other_do_not_overlap)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{3.0f, 0.0f, 0.0f});
    rig.run(walk(Vec2{1.0f, 0.0f}), walk(Vec2{-1.0f, 0.0f}), 240);
    const f32 reach = rig.a.tuning().radius + rig.b.tuning().radius;
    CHECK(rig.apart() > reach - 0.05f);
}

TEST(characters, a_character_that_is_not_solid_lets_others_through)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{3.0f, 0.0f, 0.0f});
    rig.jolt.characterSetSolid(1, false);
    CHECK(!rig.jolt.characterSolid(1));
    rig.run(walk(Vec2{1.0f, 0.0f}), MoveCommand{}, 180);
    CHECK(rig.a.state().pos.x > 3.5f);
}

TEST(characters, destroying_a_character_frees_its_slot)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{3.0f, 0.0f, 0.0f});
    rig.jolt.characterDestroy(1);
    CHECK(!rig.jolt.characterValid(1));
    rig.run(walk(Vec2{1.0f, 0.0f}), MoveCommand{}, 180);
    CHECK(rig.a.state().pos.x > 3.5f);
}

TEST(movement_ghost, holstering_takes_the_holster_time_and_drawing_the_draw_time)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    CHECK(rig.a.state().holstered);
    CHECK_NEAR(rig.a.state().holster, 1.0f, 1e-5);

    MoveCommand draw;
    draw.holster = true;
    rig.a.tick(draw, nullptr, kDt);
    CHECK(!rig.a.state().holstered);
    const i32 draw_ticks = static_cast<i32>(rig.a.tuning().draw_time / kDt);
    rig.run(MoveCommand{}, MoveCommand{}, draw_ticks);
    CHECK_NEAR(rig.a.state().holster, 0.0f, 1e-3);

    rig.a.tick(draw, nullptr, kDt);
    CHECK(rig.a.state().holstered);
    rig.run(MoveCommand{}, MoveCommand{}, static_cast<i32>(rig.a.tuning().holster_time / kDt));
    CHECK_NEAR(rig.a.state().holster, 1.0f, 1e-3);
}

TEST(movement_ghost, aiming_needs_the_gun_out_and_slows_the_walk)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    MoveCommand aim = walk(Vec2{0.0f, 1.0f});
    aim.aim = true;
    rig.run(aim, MoveCommand{}, 10);
    CHECK(!rig.a.state().aiming);

    MoveCommand draw;
    draw.holster = true;
    rig.a.tick(draw, nullptr, kDt);
    rig.run(aim, MoveCommand{}, 120);
    CHECK(rig.a.state().aiming);
    const Vec3 v = rig.a.state().vel;
    CHECK_NEAR(length(Vec3{v.x, 0.0f, v.z}), rig.a.tuning().aim_speed, 0.05);
}

TEST(movement_ghost, the_downed_cannot_move_and_lie_low)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    rig.a.set_vitals(0.0f, 0.0f, true);
    MoveCommand cmd = walk(Vec2{0.0f, 1.0f});
    cmd.jump = true;
    cmd.crawl = true;
    rig.run(cmd, MoveCommand{}, 60);
    CHECK_NEAR(rig.a.state().pos.z, 0.0f, 0.05);
    CHECK(rig.a.state().stance == Stance::Stand);
    CHECK_NEAR(rig.a.state().height, rig.a.tuning().downed_height, 1e-4);
}

TEST(movement_ghost, looking_round_while_lying_rolls_the_body_over)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    MoveCommand crawl;
    crawl.crawl = true;
    rig.a.tick(crawl, nullptr, kDt);
    CHECK(rig.a.state().stance == Stance::Crawl);
    rig.a.look(kPi, 0.0f);
    rig.run(MoveCommand{}, MoveCommand{}, 120);
    CHECK(std::cos(rig.a.state().roll) < -0.9f);
    const ghost::game::PlayerState gs = ghost_state(rig.a.state());
    CHECK(gs.onBack());
    CHECK(gs.stance == ghost::game::Stance::Crawl);
}

TEST(movement_ghost, haste_speeds_the_walk_and_shroud_wears_off)
{
    PairRig rig;
    rig.setup(Vec3{0.0f, 0.0f, 0.0f}, Vec3{5.0f, 0.0f, 0.0f});
    rig.a.apply_haste(5.0f, 1.5f);
    rig.a.apply_shroud(0.5f);
    CHECK(rig.a.hidden_from_ghosts());
    rig.run(walk(Vec2{0.0f, 1.0f}), MoveCommand{}, 120);
    const Vec3 v = rig.a.state().vel;
    const MoveTuning& t = rig.a.tuning();
    CHECK_NEAR(length(Vec3{v.x, 0.0f, v.z}), t.walk_speed * 1.5f * t.holstered_speed, 0.05);
    CHECK(!rig.a.hidden_from_ghosts());
}

TEST(movement_ghost, the_ghost_state_is_in_the_players_own_frame)
{
    MoveState s;
    s.frame = quat_from_to(Vec3{0.0f, 1.0f, 0.0f}, Vec3{1.0f, 0.0f, 0.0f});
    s.pos = Vec3{2.0f, 0.0f, 0.0f};
    s.vel = Vec3{3.0f, 0.0f, 0.0f};
    const ghost::game::PlayerState gs = ghost_state(s);
    CHECK_NEAR(gs.position.y, 2.0f, 1e-5);
    CHECK_NEAR(gs.velocity.y, 3.0f, 1e-5);
}

TEST(slots, the_local_player_starts_in_slot_zero_and_others_join_beside_them)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.sim->slot(0) != nullptr);
    CHECK(rig.sim->slot(1) == nullptr);
    const PlayerId second = rig.sim->add_player("Second");
    CHECK(second == 1);
    CHECK(rig.sim->roster().find(second) != nullptr);
    CHECK(length(rig.sim->player(0).pos() - rig.sim->player(1).pos()) > 0.8f);

    rig.sim->remove_player(second);
    CHECK(rig.sim->slot(second) == nullptr);
    CHECK(rig.sim->roster().find(second) == nullptr);
    CHECK(rig.sim->add_player("Again") == second);
}

TEST(slots, each_player_moves_from_their_own_command)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    rig.sim->add_player("Second");
    rig.tick({}, 60);
    const Vec3 a0 = rig.sim->player(0).pos();
    const Vec3 b0 = rig.sim->player(1).pos();
    Vec3 away = b0 - rig.sim->phys().body(rig.sim->vehicle().body())->pos;
    away.y = 0.0f;
    away = normalize(away) * 10.0f;

    SlotCommand cmds[2];
    cmds[0].id = 0;
    cmds[0].cmd = rig.look_at(0, a0 + Vec3{0.0f, 1.4f, -10.0f});
    cmds[1].id = 1;
    cmds[1].cmd = rig.look_at(1, b0 + away + Vec3{0.0f, 1.4f, 0.0f});
    cmds[1].cmd.move_z = 1.0f;
    cmds[1].cmd.face_view = true;
    for (u32 i = 0; i < 120; i++) {
        cmds[1].cmd.view_origin = rig.sim->player(1).pos() + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
        rig.tick(cmds, 1);
    }
    CHECK(length(rig.sim->player(0).pos() - a0) < 0.2f);
    CHECK(length(rig.sim->player(1).pos() - b0) > 2.0f);
}

TEST(slots, a_player_cannot_enter_a_car_someone_is_driving)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    rig.sim->add_player("Second");
    Sim& sim = *rig.sim;
    const RigidBody* car = sim.phys().body(sim.vehicle().body());
    if (!car) {
        FAIL("no car");
        return;
    }
    const Vec3 side = rotate(car->rot, Vec3{-(car->half_extents.x + 0.6f), 0.0f, 0.0f});
    const Vec3 door = car->pos + side;
    const f32 yaw = std::atan2(-side.x, side.z);
    sim.player(0).init(Vec3{door.x, car->pos.y - car->half_extents.y, door.z}, yaw);
    sim.player(1).init(Vec3{door.x, car->pos.y - car->half_extents.y, door.z} + rotate(car->rot, Vec3{0.0f, 0.0f, 1.5f}), yaw);
    sim.carsys().door_target[0] = true;
    sim.carsys().door_open[0] = 1.0f;
    sim.carsys().door_target[1] = true;
    sim.carsys().door_open[1] = 1.0f;
    rig.tick({}, 30);
    const Vec3 box = sim.boxes().box(IBOX_DOOR).center;
    const Vec3 door_box = car->pos + rotate(car->rot, Vec3{-f_abs(box.x), box.y, box.z} - sim.vehicle().config().com_offset);

    sim.player(0).set_exit_pref(0);
    PlayerCommand enter;
    enter.gameplay = true;
    enter.interact = false;
    for (u32 i = 0; i < 240 && sim.driver() == kNoPlayer; i++) {
        SlotCommand c[1];
        c[0].id = 0;
        c[0].cmd = rig.look_at(0, door_box);
        c[0].cmd.use_down = true;
        c[0].cmd.use_pressed = i % 4 == 0;
        rig.tick(c, 1);
    }
    CHECK(sim.driver() == 0);

    for (u32 i = 0; i < 240; i++) {
        SlotCommand c[1];
        c[0].id = 1;
        c[0].cmd = rig.look_at(1, door_box);
        c[0].cmd.use_down = true;
        c[0].cmd.use_pressed = i % 4 == 0;
        rig.tick(c, 1);
    }
    CHECK(sim.player(1).state() == PlayerState::OnFoot);
    CHECK(sim.interact(1).action() != InteractAction::EnterCar);
    CHECK(sim.driver() == 0);
}

TEST(slots, a_downed_player_is_revived_by_a_friend_holding_use)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    Sim& sim = *rig.sim;
    sim.add_player("Friend");
    rig.tick({}, 30);
    sim.roster().hurt(0, 2.0f, glm::vec3(0.0f), sim.events());
    CHECK(has_event_downed(sim.events()));
    rig.tick({}, 2);
    CHECK(sim.player(0).movement().state().downed);

    const f32 revive = sim.player_rules().reviveTime;
    SlotCommand help[1];
    help[0].id = 1;
    help[0].cmd = rig.look_at(1, sim.player(0).pos());
    help[0].cmd.use_down = true;
    rig.tick(help, static_cast<u32>((revive + 0.2f) / kFixedDt));
    CHECK(!sim.roster().downed(0));
    CHECK(!sim.player(0).movement().state().downed);
}

TEST(slots, everyone_down_puts_everyone_back_at_the_start)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    Sim& sim = *rig.sim;
    const Vec3 start = sim.player(0).pos();
    sim.player(0).init(start + Vec3{6.0f, 1.0f, 6.0f}, 0.0f);
    rig.tick({}, 30);
    sim.roster().hurt(0, 2.0f, glm::vec3(0.0f), sim.events());
    rig.tick({}, 2);
    CHECK(!sim.roster().downed(0));
    CHECK(length(sim.player(0).pos() - start) < 1.0f);
}

TEST(slots, the_dummy_joins_through_a_command_and_runs_its_script)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    Sim& sim = *rig.sim;
    rig.tick({}, 30);
    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.dummy_script = static_cast<i32>(DummyScript::Run);
    rig.tick(c, 1);
    const PlayerSlot* dummy = sim.slot(1);
    CHECK(dummy != nullptr);
    if (!dummy) {
        return;
    }
    CHECK(dummy->dummy);
    CHECK(dummy->script == DummyScript::Run);
    const Vec3 at = dummy->player.pos();
    rig.tick({}, 240);
    CHECK(length(sim.player(1).pos() - at) > 3.0f);

    c[0].cmd.dummy_script = -1;
    c[0].cmd.dummy_cycle = true;
    rig.tick(c, 1);
    CHECK(sim.slot(1)->script == DummyScript::Shroud);
    CHECK(sim.slot(2) == nullptr);
}

TEST(slots, the_crawl_dummy_goes_down_on_its_belly)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    Sim& sim = *rig.sim;
    rig.tick({}, 30);
    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.dummy_script = static_cast<i32>(DummyScript::Crawl);
    rig.tick(c, 1);
    bool crawled = false;
    bool dived = false;
    for (u32 i = 0; i < 720; i++) {
        rig.tick({}, 1);
        crawled = crawled || sim.player(1).stance() == Stance::Crawl;
        dived = dived || sim.player(1).stance() == Stance::Dive;
    }
    CHECK(crawled);
    CHECK(dived);
}

TEST(slots, a_world_with_a_dummy_stays_bit_identical)
{
    SimRig a;
    SimRig b;
    if (!a.setup() || !b.setup()) {
        FAIL("sim init");
        return;
    }
    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.dummy_script = static_cast<i32>(DummyScript::Crawl);
    a.tick(c, 1);
    b.tick(c, 1);
    for (u32 i = 0; i < 600; i++) {
        a.tick({}, 1);
        b.tick({}, 1);
        if (a.sim->checksum() != b.sim->checksum()) {
            FAIL("checksums diverged");
            return;
        }
    }
    CHECK(a.sim->slot(1) != nullptr);
}
