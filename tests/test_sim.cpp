#include "test.h"

#include "core/arena.h"
#include "math/glm_bridge.h"
#include "sim/sim.h"

#include <memory>
#include <variant>
#include <vector>

using namespace anom;

namespace {
constexpr const char* kZoneDir = "assets/zones/testzone";
constexpr u32 kWalkTicks = 240;
constexpr u32 kDriveTicks = 480;

struct SimRig {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();

    bool setup() { return sim->init(perm, scratch, kZoneDir); }

    void run(const std::vector<PlayerCommand>& commands)
    {
        for (const PlayerCommand& cmd : commands) {
            sim->tick(cmd, kFixedDt);
            sim->events().clear();
        }
    }
};

PlayerCommand gameplay_command(const Sim& sim)
{
    PlayerCommand cmd;
    cmd.gameplay = true;
    cmd.view_origin = sim.player(0).pos() + Vec3{0.0f, kPlayerEyeHeight, 0.0f};
    cmd.view_dir = Vec3{0.0f, -0.2f, -1.0f};
    return cmd;
}

std::vector<PlayerCommand> scripted_session(const Sim& sim)
{
    std::vector<PlayerCommand> out;
    for (u32 i = 0; i < kWalkTicks; i++) {
        PlayerCommand cmd = gameplay_command(sim);
        cmd.move_z = 1.0f;
        cmd.move_x = i < kWalkTicks / 2 ? 0.4f : -0.4f;
        cmd.run = i > 60;
        cmd.jump = i == 100;
        cmd.look_dx = i % 30 == 0 ? 12.0f : 0.0f;
        out.push_back(cmd);
    }
    for (u32 i = 0; i < kDriveTicks; i++) {
        PlayerCommand cmd = gameplay_command(sim);
        cmd.throttle = 1.0f;
        cmd.steer = i < kDriveTicks / 2 ? 0.5f : -0.5f;
        cmd.use_down = i < 4;
        cmd.use_pressed = i == 0;
        out.push_back(cmd);
    }
    return out;
}

}

namespace {
template <typename T>
u32 count_events(const ghost::game::EventList& events)
{
    u32 n = 0;
    for (const ghost::game::GameEvent& e : events) {
        n += std::holds_alternative<T>(e) ? 1u : 0u;
    }
    return n;
}

template <typename T>
const T* find_event(const ghost::game::EventList& events)
{
    for (const ghost::game::GameEvent& e : events) {
        if (const T* found = std::get_if<T>(&e)) {
            return found;
        }
    }
    return nullptr;
}
}

TEST(sim, a_crash_emits_one_impact_event_carrying_its_severity)
{
    SimRig rig;
    CHECK(rig.setup());
    rig.run(std::vector<PlayerCommand>(150, PlayerCommand{}));

    RigidBody* car = rig.sim->phys().body(rig.sim->vehicle().body());
    CHECK(car != nullptr);
    const Vec3 fwd = rotate(car->rot, Vec3{0.0f, 0.0f, -1.0f});
    const Vec3 wall = car->pos + fwd * 8.0f;
    rig.sim->physics().addStaticBox(to_glm(wall), glm::vec3(4.0f, 2.0f, 2.0f), 0);
    car->vel = fwd * 14.0f;

    u32 impacts = 0;
    f32 strength = 0.0f;
    for (u32 i = 0; i < 240; i++) {
        rig.sim->tick(PlayerCommand{}, kFixedDt);
        if (const auto* impact = find_event<ghost::game::CarImpact>(rig.sim->events())) {
            impacts += count_events<ghost::game::CarImpact>(rig.sim->events());
            strength = impact->strength;
        }
        rig.sim->events().clear();
    }
    CHECK(impacts >= 1);
    CHECK(impacts <= 2);
    CHECK(strength > 0.0f);
    CHECK(rig.sim->carsys().impact_serial >= 1);
}

TEST(sim, car_switches_emit_events)
{
    SimRig rig;
    CHECK(rig.setup());
    rig.run(std::vector<PlayerCommand>(10, PlayerCommand{}));

    CarSys& sys = rig.sim->carsys();
    sys.handbrake_latched = false;
    sys.headlight_switch = true;
    sys.wiper_mode = 1;
    sys.key_inserted = true;
    rig.sim->tick(PlayerCommand{}, kFixedDt);

    const auto* lever = find_event<ghost::game::HandbrakeMoved>(rig.sim->events());
    CHECK(lever != nullptr);
    CHECK(lever && !lever->set);
    const auto* lights = find_event<ghost::game::HeadlightsSwitched>(rig.sim->events());
    CHECK(lights && lights->on);
    const auto* wipers = find_event<ghost::game::WipersSwitched>(rig.sim->events());
    CHECK(wipers && wipers->mode == 1);
    const auto* key = find_event<ghost::game::KeyMoved>(rig.sim->events());
    CHECK(key && key->inserted);
    rig.sim->events().clear();

    rig.sim->tick(PlayerCommand{}, kFixedDt);
    CHECK(count_events<ghost::game::HandbrakeMoved>(rig.sim->events()) == 0);
    CHECK(count_events<ghost::game::HeadlightsSwitched>(rig.sim->events()) == 0);
}

TEST(sim, an_engine_that_runs_dry_emits_a_stall)
{
    SimRig rig;
    CHECK(rig.setup());
    rig.run(std::vector<PlayerCommand>(10, PlayerCommand{}));

    CarSys& sys = rig.sim->carsys();
    sys.key_inserted = true;
    sys.engine_on = true;
    sys.fluids.fuel = 0.5f;
    rig.sim->tick(PlayerCommand{}, kFixedDt);
    CHECK(sys.engine_on);
    rig.sim->events().clear();

    sys.fluids.fuel = 0.0f;
    rig.sim->tick(PlayerCommand{}, kFixedDt);

    const auto* stopped = find_event<ghost::game::EngineStopped>(rig.sim->events());
    CHECK(stopped != nullptr);
    CHECK(stopped && stopped->stalled);
    CHECK(!sys.engine_on);
    CHECK(sys.stall_notice > 0.0f);
    rig.sim->events().clear();

    sys.fluids.fuel = 0.5f;
    sys.engine_on = true;
    rig.sim->tick(PlayerCommand{}, kFixedDt);
    rig.sim->events().clear();
    sys.stall_notice = 0.0f;
    sys.stop_engine();
    rig.sim->tick(PlayerCommand{}, kFixedDt);
    const auto* switched = find_event<ghost::game::EngineStopped>(rig.sim->events());
    CHECK(switched != nullptr);
    CHECK(switched && !switched->stalled);
}

TEST(sim, materials_are_poured_only_on_request)
{
    SimRig rig;
    CHECK(rig.setup());
    rig.sim->carsys().parts[PART_TANK].installed = true;
    ghost::game::MaterialInventory& carried = rig.sim->gameplay().materials();
    for (u32 i = 0; i < 5; i++) {
        carried.add(static_cast<ghost::game::MaterialId>(0));
    }
    CHECK(carried.total() == 5);

    rig.run(std::vector<PlayerCommand>(120, PlayerCommand{}));
    CHECK(carried.total() == 5);
    CHECK(rig.sim->carsys().synth.tank_total() == 0u);

    rig.sim->fill_tank(0);
    CHECK(carried.total() == 0);
    CHECK(rig.sim->carsys().synth.tank_total() == 5u);
}

TEST(sim, two_sims_fed_the_same_commands_stay_bit_identical)
{
    SimRig a;
    SimRig b;
    if (!a.setup() || !b.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(a.sim->checksum() == b.sim->checksum());

    const std::vector<PlayerCommand> session = scripted_session(*a.sim);
    for (const PlayerCommand& cmd : session) {
        a.sim->tick(cmd, kFixedDt);
        b.sim->tick(cmd, kFixedDt);
        a.sim->events().clear();
        b.sim->events().clear();
        if (a.sim->checksum() != b.sim->checksum()) {
            FAIL("checksums diverged");
            return;
        }
    }
    CHECK(a.sim->tick_count() == static_cast<u64>(session.size()));
}

TEST(sim, replaying_a_recorded_session_reproduces_the_world)
{
    SimRig live;
    if (!live.setup()) {
        FAIL("sim init");
        return;
    }
    const std::vector<PlayerCommand> session = scripted_session(*live.sim);
    live.run(session);
    const u64 recorded = live.sim->checksum();

    SimRig replay;
    if (!replay.setup()) {
        FAIL("sim init");
        return;
    }
    replay.run(session);
    CHECK(replay.sim->checksum() == recorded);
    CHECK_NEAR(length(replay.sim->player(0).pos() - live.sim->player(0).pos()), 0.0f, 1e-6);
}

TEST(sim, commands_move_the_world_and_idle_commands_do_not_walk)
{
    SimRig idle;
    SimRig walked;
    if (!idle.setup() || !walked.setup()) {
        FAIL("sim init");
        return;
    }

    const Vec3 start = idle.sim->player(0).pos();
    std::vector<PlayerCommand> still(kWalkTicks, gameplay_command(*idle.sim));
    idle.run(still);
    walked.run(scripted_session(*walked.sim));

    CHECK(length(idle.sim->player(0).pos() - start) < 0.2f);
    CHECK(length(walked.sim->player(0).pos() - start) > 2.0f);
    CHECK(idle.sim->checksum() != walked.sim->checksum());
}

TEST(sim, a_non_gameplay_command_freezes_player_input)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    for (u32 i = 0; i < 60; i++) {
        PlayerCommand settle = gameplay_command(*rig.sim);
        rig.sim->tick(settle, kFixedDt);
    }
    const Vec3 start = rig.sim->player(0).pos();
    for (u32 i = 0; i < kWalkTicks; i++) {
        PlayerCommand cmd = gameplay_command(*rig.sim);
        cmd.gameplay = false;
        cmd.move_z = 1.0f;
        cmd.run = true;
        rig.sim->tick(cmd, kFixedDt);
    }
    CHECK(length(rig.sim->player(0).pos() - start) < 0.2f);
}

TEST(sim, a_queued_photo_is_taken_inside_the_tick)
{
    SimRig rig;
    if (!rig.setup()) {
        FAIL("sim init");
        return;
    }
    rig.sim->events().clear();
    std::vector<u8> frame(kPhotoBytes, 128);
    rig.sim->queue_photo(frame.data());
    CHECK(rig.sim->events().empty());

    rig.sim->tick(gameplay_command(*rig.sim), kFixedDt);
    bool photo = false;
    for (const ghost::game::GameEvent& event : rig.sim->events()) {
        photo = photo || std::holds_alternative<ghost::game::PhotoTaken>(event);
    }
    CHECK(photo);
}
