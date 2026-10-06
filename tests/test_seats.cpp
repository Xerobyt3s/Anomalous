#include "test.h"

#include "core/arena.h"
#include "math/glm_bridge.h"
#include "sim/sim.h"

#include <cmath>
#include <memory>
#include <span>

using namespace anom;

namespace {

struct SeatRig {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();

    bool setup()
    {
        if (!sim->init(perm, scratch, "assets/zones/testzone")) {
            return false;
        }
        sim->add_player("Second");
        sim->carsys().parts[PART_COMPUTER].installed = false;
        for (u32 side = 0; side < 2; side++) {
            sim->carsys().door_target[side] = true;
            sim->carsys().door_open[side] = 1.0f;
        }
        return true;
    }

    const RigidBody& car() const { return *sim->phys().body(sim->vehicle().body()); }

    void tick(std::span<const SlotCommand> commands, u32 ticks)
    {
        for (u32 i = 0; i < ticks; i++) {
            sim->tick(commands, kFixedDt);
            sim->events().clear();
        }
    }

    void idle(u32 ticks)
    {
        SlotCommand c[2];
        c[0].id = 0;
        c[1].id = 1;
        c[0].cmd.gameplay = true;
        c[1].cmd.gameplay = true;
        tick(c, ticks);
    }

    Vec3 door_box(u32 side) const
    {
        const Vec3 box = const_cast<Sim&>(*sim).boxes().box(IBOX_DOOR).center;
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        return car().pos + rotate(car().rot, Vec3{sign * f_abs(box.x), box.y, box.z} - sim->vehicle().config().com_offset);
    }

    void stand_at_door(PlayerId id, u32 side)
    {
        const f32 sign = side == 0 ? -1.0f : 1.0f;
        const Vec3 out = rotate(car().rot, Vec3{sign * (car().half_extents.x + 0.6f), 0.0f, 0.0f});
        const Vec3 door = car().pos + out;
        sim->player(id).init(Vec3{door.x, car().pos.y - car().half_extents.y, door.z}, std::atan2(-out.x, out.z));
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

    Vec3 doorway(u32 side) const { return door_box(side) + rotate(car().rot, Vec3{0.0f, 0.0f, 0.3f}); }

    bool enter(PlayerId id, u32 side)
    {
        stand_at_door(id, side);
        idle(20);
        for (u32 i = 0; i < 240 && sim->player(id).state() != PlayerState::Driving; i++) {
            SlotCommand c[1];
            c[0].id = id;
            c[0].cmd = look_at(id, doorway(side));
            c[0].cmd.use_pressed = sim->interact(id).action() == InteractAction::EnterCar;
            c[0].cmd.use_down = c[0].cmd.use_pressed;
            tick(c, 1);
        }
        return sim->player(id).state() == PlayerState::Driving;
    }
};

}

TEST(seats, the_excel_has_a_driver_and_a_passenger_seat_blocked_by_the_computer)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    const VehicleConfig& cfg = rig.sim->vehicle().config();
    CHECK(cfg.seat_count == 2u);
    CHECK(cfg.seats[0].drives);
    CHECK(cfg.seats[0].door_side == 0);
    CHECK(!cfg.seats[1].drives);
    CHECK(cfg.seats[1].door_side == 1);
    CHECK(cfg.seats[1].blocked_by == PART_COMPUTER);
    CHECK(cfg.seats[1].eye.x > 0.0f);
}

TEST(seats, a_passenger_rides_in_the_right_seat_while_the_driver_drives)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(0, 0));
    const bool in = rig.enter(1, 1);
    CHECK(in);
    CHECK(rig.sim->player(0).seat() == 0u);
    CHECK(rig.sim->player(1).seat() == 1u);
    CHECK(rig.sim->driver() == 0);
    CHECK(rig.sim->seat_owner(1) == 1);

    const Vec3 start = rig.car().pos;
    SlotCommand c[1];
    c[0].id = 1;
    c[0].cmd.gameplay = true;
    c[0].cmd.throttle = 1.0f;
    rig.tick(c, 240);
    CHECK(length(rig.car().pos - start) < 0.3f);

    const Vec3 eye = rig.sim->player(1).pos() + rotate(rig.car().rot, Vec3{0.0f, kPlayerEyeHeight, 0.0f});
    const Vec3 local = rotate(conjugate(rig.car().rot), eye - rig.car().pos);
    CHECK(local.x > 0.2f);
}

TEST(seats, a_passenger_is_offered_no_driver_controls)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(0, 0));
    CHECK(rig.enter(1, 1));

    const Vec3 com = rig.sim->vehicle().config().com_offset;
    const Vec3 lever = rig.car().pos + rotate(rig.car().rot, rig.sim->boxes().box(IBOX_HANDBRAKE).center - com);
    SlotCommand c[2];
    c[0].id = 0;
    c[0].cmd = rig.look_at(0, lever);
    c[1].id = 1;
    c[1].cmd = rig.look_at(1, lever);
    rig.tick(c, 2);

    CHECK(rig.sim->interact(0).action() == InteractAction::Handbrake);
    CHECK(rig.sim->interact(1).action() == InteractAction::Info);
    CHECK(rig.sim->interact(1).prompt() == "driver's controls");
}

TEST(controls, a_handbrake_tap_toggles_the_latch_and_a_hold_is_momentary)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(0, 0));
    CarSys& sys = rig.sim->carsys();
    sys.handbrake_latched = true;

    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.handbrake_toggle = true;
    rig.tick(c, 1);
    CHECK(!sys.handbrake_latched);

    c[0].cmd.handbrake_toggle = false;
    c[0].cmd.handbrake = true;
    rig.tick(c, 30);
    CHECK(!sys.handbrake_latched);
    CHECK(rig.sim->vehicle().input().handbrake);

    c[0].cmd.handbrake = false;
    rig.tick(c, 1);
    CHECK(!rig.sim->vehicle().input().handbrake);

    c[0].cmd.handbrake_toggle = true;
    rig.tick(c, 1);
    CHECK(sys.handbrake_latched);
    c[0].cmd.handbrake_toggle = false;
    rig.tick(c, 1);
    CHECK(rig.sim->vehicle().input().handbrake);
}

TEST(controls, an_ignition_tap_stops_the_engine_and_inserts_a_held_key)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(0, 0));
    rig.sim->interact(0).set_has_key(true);
    CarSys& sys = rig.sim->carsys();
    CHECK(!sys.key_inserted);

    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.ignition_tap = true;
    rig.tick(c, 1);
    CHECK(sys.key_inserted);
    CHECK(!rig.sim->interact(0).has_key());

    c[0].cmd.ignition_tap = false;
    sys.engine_on = true;
    sys.fluids.fuel = 0.5f;
    rig.tick(c, 1);
    CHECK(sys.engine_on);

    c[0].cmd.ignition_tap = true;
    rig.tick(c, 1);
    CHECK(!sys.engine_on);
    CHECK(sys.stall_notice == 0.0f);

    c[0].cmd.ignition_tap = false;
    c[0].cmd.horn = true;
    rig.tick(c, 1);
    CHECK(sys.horn_on);
    c[0].cmd.horn = false;
    rig.tick(c, 1);
    CHECK(!sys.horn_on);
}

TEST(seats, the_computer_on_the_passenger_seat_keeps_it_closed)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    rig.sim->carsys().parts[PART_COMPUTER].installed = true;
    CHECK(!rig.enter(1, 1));
    CHECK(rig.sim->interact(1).action() == InteractAction::Info);
    CHECK(rig.sim->interact(1).prompt().find("on that seat") != std::string_view::npos);

    rig.sim->carsys().parts[PART_COMPUTER].installed = false;
    CHECK(rig.enter(1, 1));
    rig.sim->carsys().parts[PART_COMPUTER].installed = true;
    rig.idle(4);
    CHECK(rig.sim->player(1).state() == PlayerState::OnFoot);
}

TEST(seats, a_passenger_shoots_out_of_the_window_but_not_through_the_car)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(1, 1));
    rig.idle(80);
    SlotCommand c[1];
    c[0].id = 1;
    c[0].cmd.gameplay = true;
    c[0].cmd.holster = true;
    rig.tick(c, 1);
    c[0].cmd.holster = false;
    rig.tick(c, 90);
    CHECK(!rig.sim->player(1).movement().state().holstered);

    const Quat rot = rig.car().rot;
    const Vec3 eye = rig.car().pos + rotate(rot, rig.sim->vehicle().config().seats[1].eye);
    c[0].cmd.muzzle = eye;
    c[0].cmd.barrel_dir = rotate(rot, Vec3{0.0f, 0.0f, -1.0f});
    c[0].cmd.trigger = true;
    rig.tick(c, 40);
    CHECK(rig.sim->slot(1)->window_blocked);
    CHECK(rig.sim->gameplay().ballistics().projectiles().empty());

    c[0].cmd.trigger = false;
    rig.tick(c, 20);
    c[0].cmd.barrel_dir = rotate(rot, normalize(Vec3{1.0f, 0.05f, -0.2f}));
    c[0].cmd.trigger = true;
    bool fired = false;
    for (u32 i = 0; i < 60 && !fired; i++) {
        rig.tick(c, 1);
        for (const ghost::game::Projectile& p : rig.sim->gameplay().ballistics().projectiles()) {
            const Vec3 local = rotate(conjugate(rot), from_glm(p.previousPosition) - rig.car().pos);
            CHECK(local.x > rig.car().half_extents.x);
            fired = true;
        }
    }
    CHECK(fired);
    CHECK(!rig.sim->slot(1)->window_blocked);
}

TEST(seats, the_driver_cannot_draw_the_gun)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(0, 0));
    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.holster = true;
    rig.tick(c, 1);
    c[0].cmd.holster = false;
    rig.tick(c, 90);
    CHECK(rig.sim->player(0).movement().state().holstered);
    CHECK(!rig.sim->seated_armed(*rig.sim->slot(0)));
}

TEST(seats, a_bullet_inherits_the_velocity_it_is_fired_with)
{
    ghost::game::Ballistics still;
    ghost::game::Ballistics moving;
    ghost::game::ShotProfile profile;
    still.fire(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), ghost::game::Round{}, profile);
    profile.inheritVelocity = glm::vec3(0.0f, 0.0f, -20.0f);
    moving.fire(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), ghost::game::Round{}, profile);
    const glm::vec3 delta = moving.projectiles().front().velocity - still.projectiles().front().velocity;
    CHECK(glm::length(delta - glm::vec3(0.0f, 0.0f, -20.0f)) < 1e-3f);
}

TEST(seats, a_passenger_cannot_aim_while_the_cylinder_is_open)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(1, 1));
    rig.idle(80);
    SlotCommand c[1];
    c[0].id = 1;
    c[0].cmd.gameplay = true;
    c[0].cmd.holster = true;
    rig.tick(c, 1);
    c[0].cmd.holster = false;
    rig.tick(c, 90);
    c[0].cmd.cylinder = true;
    rig.tick(c, 1);
    c[0].cmd.cylinder = false;
    c[0].cmd.aim = true;
    rig.tick(c, 40);
    CHECK(!rig.sim->slot(1)->gun.mechanism.state().isClosed());
    CHECK(!rig.sim->player(1).movement().state().aiming);
    c[0].cmd.close_cylinder = true;
    rig.tick(c, 1);
    c[0].cmd.close_cylinder = false;
    rig.tick(c, 90);
    CHECK(rig.sim->slot(1)->gun.mechanism.state().isClosed());
    CHECK(rig.sim->player(1).movement().state().aiming);
}

TEST(seats, a_handbrake_tap_only_sets_the_parking_brake_when_the_car_is_stopped)
{
    SeatRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(rig.enter(0, 0));
    rig.sim->carsys().handbrake_latched = false;
    SlotCommand c[1];
    c[0].id = 0;
    c[0].cmd.gameplay = true;
    c[0].cmd.handbrake_toggle = true;
    rig.tick(c, 1);
    CHECK(rig.sim->carsys().handbrake_latched);
    rig.tick(c, 1);
    CHECK(!rig.sim->carsys().handbrake_latched);

    RigidBody* car = rig.sim->phys().body(rig.sim->vehicle().body());
    car->vel = rotate(car->rot, Vec3{0.0f, 0.0f, -20.0f});
    rig.tick(c, 1);
    CHECK(!rig.sim->carsys().handbrake_latched);
}
