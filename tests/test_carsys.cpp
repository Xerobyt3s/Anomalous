#include "test.h"

#include "engine/physics/physics_world.h"

#include "carsys/car_lights.h"

#include "carsys/carsys.h"
#include "core/arena.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "vehicle/vehicle.h"
#include "world/entity.h"
#include "world/weather.h"

using namespace anom;

namespace {
constexpr f32 kDt = 1.0f / 120.0f;

struct CarRig {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    ghost::engine::PhysicsWorld jolt;
    Vehicle car;
    CarSys sys;

    bool setup()
    {
        hf.alloc(arena, 64, 2.0f);
        for (u32 z = 0; z < 64; z++) {
            for (u32 x = 0; x < 64; x++) {
                hf.set_height(x, z, 0.0f);
            }
        }
        hf.recompute_extents();
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        sys.init();
        return car.init(world, arena, "assets/cars/excel.cfg", Vec3{0.0f, 1.0f, 0.0f}, 0.0f);
    }

    void run(i32 ticks)
    {
        for (i32 i = 0; i < ticks; i++) {
            sys.tick(car, world, kDt);
            car.tick(world, kDt);
            world.tick(kDt);
        }
    }
};

}

TEST(carsys, starts_with_a_healthy_car)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(120);

    rig.sys.key_inserted = true;
    rig.sys.crank_request = true;
    rig.run(360);

    CHECK(rig.sys.engine_on);
    CHECK(rig.sys.elec.powered[CONSUMER_IGNITION]);
    CHECK(drivetrain_rpm(rig.car.train()) > 400.0f);
}

TEST(carsys, will_not_start_without_a_battery)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.sys.parts[PART_BATTERY].installed = false;
    rig.run(120);

    rig.sys.key_inserted = true;
    rig.sys.crank_request = true;
    rig.run(600);

    CHECK(!rig.sys.engine_on);
    CHECK(!rig.sys.elec.powered[CONSUMER_STARTER]);
}

TEST(carsys, will_not_start_without_fuel)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.sys.fluids.fuel = 0.0f;
    rig.run(120);

    rig.sys.key_inserted = true;
    rig.sys.crank_request = true;
    rig.run(600);

    CHECK(!rig.sys.engine_on);
}

TEST(carsys, will_not_start_with_a_dead_engine)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.sys.parts[PART_ENGINE].condition = 0.0f;
    rig.run(120);

    rig.sys.key_inserted = true;
    rig.sys.crank_request = true;
    rig.run(600);

    CHECK(!rig.sys.engine_on);
}

TEST(carsys, running_engine_stops_when_fuel_runs_out)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    rig.sys.key_inserted = true;
    rig.sys.crank_request = true;
    rig.run(360);
    CHECK(rig.sys.engine_on);

    rig.sys.fluids.fuel = 0.0f;
    rig.run(60);
    CHECK(!rig.sys.engine_on);
}

TEST(carsys, alternator_recharges_the_battery)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    rig.sys.key_inserted = true;
    rig.sys.crank_request = true;
    rig.run(360);
    CHECK(rig.sys.engine_on);

    rig.sys.elec.battery_charge = 0.5f;
    const f32 before = rig.sys.elec.battery_charge;
    rig.run(1200);
    CHECK(rig.sys.elec.battery_charge > before);
    CHECK(rig.sys.elec.alternator_amps > 0.0f);
}

TEST(carsys, headlights_need_power_and_working_lamps)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.sys.headlight_switch = true;
    rig.run(60);
    CHECK(rig.car.effects().headlights_on);

    rig.sys.parts[PART_HEADLIGHTS].condition = 0.0f;
    rig.run(60);
    CHECK(!rig.car.effects().headlights_on);
}

TEST(carsys, missing_tire_degrades_grip_and_radius)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    CHECK(rig.car.effects().tire_grip_mul[WHEEL_FL] > 0.9f);

    rig.sys.parts[PART_TIRE_FL].installed = false;
    rig.run(60);
    CHECK(rig.car.effects().tire_grip_mul[WHEEL_FL] < 0.2f);
    CHECK(rig.car.effects().tire_radius_mul[WHEEL_FL] < 0.7f);
}

TEST(carsys, damaged_engine_reduces_power)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    const f32 healthy = rig.car.effects().engine_power_mul;

    rig.sys.parts[PART_ENGINE].condition = 0.2f;
    rig.run(60);
    CHECK(rig.car.effects().engine_power_mul < healthy);
}

TEST(carsys, cargo_stacks_and_reports_capacity)
{
    CarSys sys;
    sys.init();
    CHECK(sys.cargo_count() == 0);

    bool ok = false;
    const f32 y0 = sys.cargo_place_y(ITEM_BATTERY, 0.0f, 1.7f, &ok);
    CHECK(ok);
    CHECK(sys.cargo_add(Item{ITEM_BATTERY, 1.0f, 0}, Vec3{0.0f, y0, 1.7f}));
    CHECK(sys.cargo_count() == 1);

    const f32 y1 = sys.cargo_place_y(ITEM_BATTERY, 0.0f, 1.7f, &ok);
    CHECK(y1 > y0);

    Item taken;
    CHECK(sys.cargo_take(0, taken));
    CHECK(taken.kind == ITEM_BATTERY);
    CHECK(sys.cargo_count() == 0);
    CHECK(!sys.cargo_take(0, taken));
}

TEST(carsys, cargo_capacity_is_bounded)
{
    CarSys sys;
    sys.init();
    u32 added = 0;
    for (u32 i = 0; i < kCargoMax + 4; i++) {
        if (sys.cargo_add(Item{ITEM_FLOPPY, 1.0f, 0}, Vec3{0.0f, 0.0f, 1.7f})) {
            added++;
        }
    }
    CHECK(added == kCargoMax);
    CHECK(sys.cargo_count() == kCargoMax);
}

TEST(carsys, impact_damages_nearby_parts)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);
    CHECK(parts[PART_RADIATOR].condition == 1.0f);

    parts_apply_impact(parts, part_def(PART_RADIATOR).socket_pos, 0.5f);
    CHECK(parts[PART_RADIATOR].condition < 0.6f);
    CHECK(parts[PART_FUEL_TANK].condition > 0.95f);
}

TEST(carsys, an_impact_bumps_the_serial_and_records_severity)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(120);

    const u32 before = rig.sys.impact_serial;
    RigidBody* body = rig.world.body(rig.car.body());
    CHECK(body != nullptr);
    rig.sys.prev_vel = Vec3{0.0f, 0.0f, -20.0f};
    body->vel = Vec3{};
    rig.sys.tick(rig.car, rig.world, kDt);

    CHECK(rig.sys.impact_serial == before + 1);
    CHECK(rig.sys.last_impact_severity > 0.5f);

    rig.sys.prev_vel = body->vel;
    rig.sys.tick(rig.car, rig.world, kDt);
    CHECK(rig.sys.impact_serial == before + 1);
}

TEST(carsys, headlights_place_two_beams_ahead_and_nothing_when_off)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    const RigidBody* body = rig.world.body(rig.car.body());
    PointLight lights[kCarLightMax];
    CHECK(car_lights(body->pos, body->rot, rig.car, rig.sys, lights) == 0u);

    rig.car.effects().headlights_on = true;
    const u32 on = car_lights(body->pos, body->rot, rig.car, rig.sys, lights);
    CHECK(on == 3u);
    CHECK(lights[0].pos.z < body->pos.z - 3.0f);
    CHECK(lights[1].pos.z < body->pos.z - 3.0f);
    CHECK(lights[0].pos.x < lights[1].pos.x);
    CHECK(lights[0].color.x > 5.0f);

    rig.sys.parts[PART_HEADLIGHTS].condition = 0.5f;
    car_lights(body->pos, body->rot, rig.car, rig.sys, lights);
    CHECK(lights[0].color.x < 5.0f);
}

TEST(carsys, a_braking_car_lights_its_tail)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    const RigidBody* body = rig.world.body(rig.car.body());
    PointLight lights[kCarLightMax];
    VehicleInput in;
    in.brake = 1.0f;
    rig.car.set_input(in);
    CHECK(car_lights(body->pos, body->rot, rig.car, rig.sys, lights) == 1u);
    CHECK(lights[0].pos.z > body->pos.z + 1.0f);
    CHECK(lights[0].color.x > lights[0].color.y);

    in.brake = 0.0f;
    in.handbrake = true;
    rig.car.set_input(in);
    CHECK(car_lights(body->pos, body->rot, rig.car, rig.sys, lights) == 1u);
}

TEST(carsys, puff_rate_grows_with_slide_and_is_zero_when_gripping)
{
    CHECK(puff_rate(0.0f) == 0.0f);
    CHECK(puff_rate(2.5f) == 0.0f);
    CHECK(puff_rate(4.0f) > 0.0f);
    CHECK(puff_rate(8.0f) > puff_rate(4.0f));
}

TEST(carsys, start_blocker_names_the_missing_thing)
{
    CarRig rig;
    CHECK(rig.setup());
    rig.run(60);
    CHECK(rig.sys.start_blocker() == StartBlocker::NoKey);
    rig.sys.key_inserted = true;
    CHECK(rig.sys.start_blocker() == StartBlocker::None);
    rig.sys.fluids.fuel = 0.0f;
    CHECK(rig.sys.start_blocker() == StartBlocker::NoFuel);
    rig.sys.fluids.fuel = 0.5f;
    rig.sys.parts[PART_ENGINE].condition = 0.0f;
    CHECK(rig.sys.start_blocker() == StartBlocker::EngineDead);
    rig.sys.parts[PART_ENGINE].condition = 1.0f;
    rig.sys.elec.battery_charge = 0.05f;
    CHECK(rig.sys.start_blocker() == StartBlocker::BatteryFlat);
    rig.sys.elec.battery_charge = 0.9f;
    rig.sys.engine_on = true;
    CHECK(rig.sys.start_blocker() == StartBlocker::Running);
    CHECK(start_blocker_text(StartBlocker::NoFuel) == "no fuel");
}

TEST(fluids, overheating_damages_the_engine)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);
    parts[PART_RADIATOR].installed = false;

    Fluids fluids;
    fluids.init();
    fluids.coolant_temp = kCoolantOverheatC + 5.0f;

    FluidsInput in;
    in.engine_on = true;
    in.fuel_pump_powered = true;
    in.rpm = 4000.0f;
    in.throttle = 1.0f;

    const f32 before = parts[PART_ENGINE].condition;
    for (i32 i = 0; i < 600; i++) {
        fluids.tick(parts, in, kDt);
    }
    CHECK(parts[PART_ENGINE].condition < before);
    CHECK(fluids.overheat_power_mul() < 1.0f);
}

TEST(fluids, radiator_keeps_temperature_down)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);

    Fluids fluids;
    fluids.init();

    FluidsInput in;
    in.engine_on = true;
    in.fuel_pump_powered = true;
    in.rpm = 3000.0f;
    in.throttle = 0.5f;
    in.speed = 20.0f;

    for (i32 i = 0; i < 6000; i++) {
        fluids.tick(parts, in, kDt);
    }
    CHECK(fluids.coolant_temp > kCoolantAmbientC);
    CHECK(fluids.coolant_temp < kCoolantOverheatC);
}

TEST(fluids, low_oil_wears_the_engine)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);

    Fluids fluids;
    fluids.init();
    fluids.oil = 0.05f;

    FluidsInput in;
    in.engine_on = true;
    in.fuel_pump_powered = true;

    for (i32 i = 0; i < 600; i++) {
        fluids.tick(parts, in, kDt);
    }
    CHECK(parts[PART_ENGINE].condition < 1.0f);
}

TEST(electrics, starter_needs_more_charge_than_the_radio)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);

    Electrics elec;
    elec.init();
    elec.battery_charge = 0.05f;

    ElectricsInput in;
    in.cranking = true;
    in.deck_on = true;
    elec.tick(parts, in, kDt);

    CHECK(!elec.powered[CONSUMER_STARTER]);
    CHECK(elec.powered[CONSUMER_DECK]);
}

TEST(electrics, a_blown_fuse_kills_only_its_consumer)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);

    Electrics elec;
    elec.init();
    elec.fuse_ok[CONSUMER_HEADLIGHTS] = false;

    ElectricsInput in;
    in.headlights_switch = true;
    in.deck_on = true;
    elec.tick(parts, in, kDt);

    CHECK(!elec.powered[CONSUMER_HEADLIGHTS]);
    CHECK(elec.powered[CONSUMER_DECK]);
}

TEST(electrics, battery_drains_under_load)
{
    PartSlot parts[PART_COUNT];
    parts_init(parts);

    Electrics elec;
    elec.init();
    const f32 before = elec.battery_charge;

    ElectricsInput in;
    in.headlights_switch = true;
    for (i32 i = 0; i < 6000; i++) {
        elec.tick(parts, in, kDt);
    }
    CHECK(elec.battery_charge < before);
}

TEST(items, ids_round_trip)
{
    for (u32 k = 1; k < ITEM_KIND_COUNT; k++) {
        const ItemKind kind = static_cast<ItemKind>(k);
        CHECK(item_from_id(item_id(kind)) == kind);
        CHECK(!item_name(kind).empty());
    }
    CHECK(item_from_id("not_a_real_item") == ITEM_NONE);
}

TEST(items, part_to_item_mapping)
{
    CHECK(item_for_part(PART_BATTERY) == ITEM_BATTERY);
    CHECK(item_for_part(PART_TIRE_RR) == ITEM_TIRE);
    CHECK(item_for_part(PART_ENGINE) == ITEM_NONE);
    CHECK(antenna_variant_for_item(ITEM_ANTENNA_ARRAY) == 2);
    CHECK(antenna_item_for_variant(0) == ITEM_ANTENNA_WHIP);
}

TEST(weather, modes_drive_rain_toward_their_target)
{
    Weather w;
    w.init(7);
    w.set_mode(WeatherMode::Rain);
    for (i32 i = 0; i < 20000; i++) {
        w.tick(kDt);
    }
    CHECK(w.rain() > 0.6f);
    CHECK(w.overcast() > 0.5f);
    CHECK(w.wetness() > 0.3f);

    w.set_mode(WeatherMode::Clear);
    for (i32 i = 0; i < 40000; i++) {
        w.tick(kDt);
    }
    CHECK(w.rain() < 0.05f);
}

TEST(weather, snow_falls_settles_and_melts)
{
    Weather w;
    w.init(11);
    w.set_mode(WeatherMode::Snow);
    for (i32 i = 0; i < 24000; i++) {
        w.tick(kDt);
    }
    CHECK(w.snow() > 0.6f);
    CHECK(w.rain() < 0.01f);
    CHECK(w.overcast() > 0.7f);
    CHECK(w.snow_cover() > 0.95f);
    CHECK(w.wetness() < 0.05f);

    w.set_mode(WeatherMode::Clear);
    for (i32 i = 0; i < 28800; i++) {
        w.tick(kDt);
    }
    const f32 after_clear = w.snow_cover();
    CHECK(after_clear < 0.95f);
    CHECK(after_clear > 0.3f);

    Weather r = w;
    r.set_mode(WeatherMode::Rain);
    for (i32 i = 0; i < 7200; i++) {
        w.tick(kDt);
        r.tick(kDt);
    }
    CHECK(r.snow_cover() < w.snow_cover());

    for (i32 i = 0; i < 60000; i++) {
        w.tick(kDt);
    }
    CHECK(w.snow_cover() == 0.0f);
}

TEST(weather, is_deterministic_for_a_seed)
{
    Weather a;
    Weather b;
    a.init(99);
    b.init(99);
    for (i32 i = 0; i < 30000; i++) {
        a.tick(kDt);
        b.tick(kDt);
    }
    CHECK(a.rain() == b.rain());
    CHECK(a.wind() == b.wind());
}

TEST(world, spawn_and_despawn_entities)
{
    Arena arena(megabytes(8));
    World world;
    world.init(arena);
    CHECK(world.count() == 0);

    const EntityHandle h = world.spawn(EntityKind::Building, Vec3{1.0f, 2.0f, 3.0f},
                                       quat_identity(), 1.0f, "garage",
                                       kEntityFlagCollides);
    CHECK(h.valid());
    CHECK(world.count() == 1);

    Entity* e = world.entity(h);
    CHECK(e != nullptr);
    CHECK(e->mesh_name == "garage");
    CHECK(e->kind == EntityKind::Building);
    CHECK((e->flags & kEntityFlagCollides) != 0);
    CHECK(e->body == kNoEntityBody);

    world.despawn(h);
    CHECK(world.count() == 0);
    CHECK(world.entity(h) == nullptr);
}

TEST(world, clear_removes_everything)
{
    Arena arena(megabytes(8));
    World world;
    world.init(arena);
    for (i32 i = 0; i < 32; i++) {
        world.spawn(EntityKind::Tree, Vec3{static_cast<f32>(i), 0.0f, 0.0f}, quat_identity(),
                    1.0f, "", 0);
    }
    CHECK(world.count() == 32);
    world.clear();
    CHECK(world.count() == 0);
    CHECK(world.entities().live_indices().empty());
}

TEST(carsys, the_roof_coil_starts_uninstalled_and_maps_to_its_item)
{
    CarRig rig;
    CHECK(rig.setup());
    CHECK(!rig.sys.parts[PART_COIL].installed);
    CHECK(item_for_part(PART_COIL) == ITEM_COIL);
    CHECK(part_def(PART_COIL).removable);
    CHECK(part_def(PART_COIL).socket_pos.y > 0.4f);
    CHECK(!part_def(PART_COIL).engine_bay);
    CHECK(part_def(PART_COIL).mesh == "part_coil");
}

TEST(items, the_coil_round_trips_through_its_id)
{
    CHECK(item_from_id("coil") == ITEM_COIL);
    CHECK(item_id(ITEM_COIL) == "coil");
    CHECK(item_mass(ITEM_COIL) > 20.0f);
}
