#include "test.h"

#include "engine/physics/physics_world.h"

#include "carsys/carsys.h"
#include "core/arena.h"
#include "physics/heightfield.h"
#include "physics/world.h"
#include "player/interact.h"
#include "player/player.h"
#include "vehicle/vehicle.h"
#include "world/entity.h"
#include "world/pickup_body.h"
#include "world/terrain.h"
#include "world/zone.h"

using namespace anom;

namespace {

constexpr f32 kDt = 1.0f / 120.0f;

struct Bay {
    Arena arena{megabytes(64)};
    Heightfield hf;
    PhysWorld world;
    ghost::engine::PhysicsWorld jolt;
    World entities;
    Vehicle car;
    CarSys sys;
    Player player;
    Interact it;
    InteractBoxes boxes;

    bool setup()
    {
        hf.alloc(arena, 96, 2.0f);
        for (u32 z = 0; z < 96; z++) {
            for (u32 x = 0; x < 96; x++) {
                hf.set_height(x, z, 0.0f);
            }
        }
        hf.recompute_extents();
        world.init(arena, &hf);
        world.set_jolt(&jolt);
        entities.init(arena);
        sys.init();
        it.init();
        boxes.init(arena, "assets/cars/excel_interact.cfg");
        player.init(Vec3{4.0f, 0.5f, 0.0f}, 0.0f);
        if (!car.init(world, arena, "assets/cars/excel.cfg", Vec3{0.0f, 1.0f, 0.0f}, 0.0f)) {
            return false;
        }
        car.effects().ignition_ok = false;
        settle(240);
        return true;
    }

    InteractContext ctx(Ray ray, bool down, bool pressed)
    {
        InteractContext c;
        c.player = &player;
        c.veh = &car;
        c.sys = &sys;
        c.world = &entities;
        c.phys = &world;
        c.view_ray = ray;
        c.e_down = down;
        c.e_pressed = pressed;
        return c;
    }

    void settle(i32 ticks)
    {
        for (i32 i = 0; i < ticks; i++) {
            sys.tick(car, world, kDt);
            car.tick(world, kDt);
            world.tick(kDt);
        }
    }

    Ray aim_at_socket(PartKind part) const
    {
        const RigidBody* body = world.body(car.body());
        const Vec3 local = part_def(part).socket_pos - car.config().com_offset;
        const Vec3 target = body->pos + rotate(body->rot, local);
        const Vec3 eye = target + Vec3{0.0f, 0.0f, 0.0f} + Vec3{1.4f, 0.9f, 0.0f};
        Ray r;
        r.origin = eye;
        r.dir = normalize(target - eye);
        return r;
    }

    void hold(Ray ray, i32 ticks)
    {
        it.update(boxes, ctx(ray, true, true), kDt);
        for (i32 i = 1; i < ticks; i++) {
            it.update(boxes, ctx(ray, true, false), kDt);
        }
        it.update(boxes, ctx(ray, false, false), kDt);
    }

    void tap(Ray ray)
    {
        it.update(boxes, ctx(ray, true, true), kDt);
        it.update(boxes, ctx(ray, false, false), kDt);
    }
};

} // namespace

TEST(interact, a_client_preview_does_not_power_a_loose_terminal)
{
    Bay bay;
    CHECK(bay.setup());
    const EntityHandle h = interact_spawn_pickup(bay.entities, bay.world, Item{ITEM_COMPUTER},
                                                 Vec3{3.0f, 0.4f, 1.0f}, 0.0f, Vec3{});
    bay.settle(60);
    PickupState s;
    CHECK(pickup_body_state(bay.world, *bay.entities.entity(h), s));

    Ray ray;
    ray.origin = s.pos + Vec3{0.8f, 1.0f, 0.0f};
    ray.dir = normalize(s.pos - ray.origin);

    InteractContext preview = bay.ctx(ray, true, true);
    preview.preview = true;
    bay.it.update(bay.boxes, preview, kDt);
    CHECK(bay.it.action() == InteractAction::Pickup);
    preview = bay.ctx(ray, false, false);
    preview.preview = true;
    bay.it.update(bay.boxes, preview, kDt);
    CHECK(!bay.sys.computer_on);

    bay.tap(ray);
    CHECK(bay.sys.computer_on);
}

TEST(interact, the_trunk_offers_to_pour_carried_materials)
{
    Bay bay;
    CHECK(bay.setup());
    bay.sys.trunk_target = true;
    bay.sys.trunk_open = 1.0f;
    bay.sys.parts[PART_TANK].installed = true;

    const RigidBody* body = bay.world.body(bay.car.body());
    const Vec3 edge = body->pos
                    + rotate(body->rot, bay.boxes.box(IBOX_TRUNK_EDGE).center - bay.car.config().com_offset);
    Ray ray;
    ray.origin = edge + Vec3{0.0f, 0.8f, 1.6f};
    ray.dir = normalize(edge - ray.origin);

    InteractContext empty = bay.ctx(ray, false, false);
    empty.materials = 0;
    bay.it.update(bay.boxes, empty, kDt);
    CHECK(bay.it.action() != InteractAction::PourMaterials);

    InteractContext carrying = bay.ctx(ray, false, false);
    carrying.materials = 7;
    bay.it.update(bay.boxes, carrying, kDt);
    CHECK(bay.it.action() == InteractAction::PourMaterials);
    CHECK(bay.it.prompt() == "[E] pour 7 materials into tank");

    carrying = bay.ctx(ray, true, true);
    carrying.materials = 7;
    bay.it.update(bay.boxes, carrying, kDt);
    carrying = bay.ctx(ray, false, false);
    carrying.materials = 7;
    bay.it.update(bay.boxes, carrying, kDt);
    CHECK(bay.it.pour_request());
    CHECK(!bay.it.pour_request());
}

TEST(interact, aiming_at_the_hood_latch_offers_to_open_it)
{
    Bay bay;
    CHECK(bay.setup());

    const RigidBody* body = bay.world.body(bay.car.body());
    const Vec3 latch = body->pos
                     + rotate(body->rot, bay.boxes.box(IBOX_HOOD_LATCH).center
                                             - bay.car.config().com_offset);
    const Vec3 eye = latch + Vec3{0.0f, 0.9f, -1.4f};
    Ray r;
    r.origin = eye;
    r.dir = normalize(latch - eye);

    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() == InteractAction::ToggleHood);
    CHECK(bay.it.prompt() == "[E] open hood");
}

TEST(interact, opening_the_hood_is_a_tap)
{
    Bay bay;
    CHECK(bay.setup());

    const RigidBody* body = bay.world.body(bay.car.body());
    const Vec3 latch = body->pos
                     + rotate(body->rot, bay.boxes.box(IBOX_HOOD_LATCH).center
                                             - bay.car.config().com_offset);
    const Vec3 eye = latch + Vec3{0.0f, 0.9f, -1.4f};
    Ray r;
    r.origin = eye;
    r.dir = normalize(latch - eye);

    CHECK(!bay.sys.hood_target);
    bay.tap(r);
    CHECK(bay.sys.hood_target);
}

TEST(interact, removing_the_battery_requires_a_hold)
{
    Bay bay;
    CHECK(bay.setup());
    bay.sys.hood_target = true;
    bay.sys.hood_open = 1.0f;

    const Ray r = bay.aim_at_socket(PART_BATTERY);
    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() == InteractAction::RemovePart);
    CHECK(bay.it.action_is_hold());

    bay.tap(r);
    CHECK(bay.sys.parts[PART_BATTERY].installed);
    CHECK(bay.it.hands().kind == ITEM_NONE);

    bay.hold(r, 200);
    CHECK(!bay.sys.parts[PART_BATTERY].installed);
    CHECK(bay.it.hands().kind == ITEM_BATTERY);
}

TEST(interact, installing_the_battery_puts_it_back)
{
    Bay bay;
    CHECK(bay.setup());
    bay.sys.hood_target = true;
    bay.sys.hood_open = 1.0f;

    const Ray r = bay.aim_at_socket(PART_BATTERY);
    bay.hold(r, 200);
    CHECK(bay.it.hands().kind == ITEM_BATTERY);
    CHECK(!bay.sys.parts[PART_BATTERY].installed);

    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() == InteractAction::InstallPart);

    bay.hold(r, 200);
    CHECK(bay.sys.parts[PART_BATTERY].installed);
    CHECK(bay.it.hands().kind == ITEM_NONE);
}

TEST(interact, a_removed_battery_stops_the_starter)
{
    Bay bay;
    CHECK(bay.setup());
    bay.sys.hood_target = true;
    bay.sys.hood_open = 1.0f;

    bay.hold(bay.aim_at_socket(PART_BATTERY), 200);
    CHECK(!bay.sys.parts[PART_BATTERY].installed);

    bay.sys.key_inserted = true;
    bay.sys.crank_request = true;
    bay.settle(600);
    CHECK(!bay.sys.engine_on);
}

TEST(interact, engine_socket_reports_condition)
{
    Bay bay;
    CHECK(bay.setup());
    bay.sys.hood_target = true;
    bay.sys.hood_open = 1.0f;

    const Ray r = bay.aim_at_socket(PART_ENGINE);
    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() == InteractAction::Info);
    CHECK(bay.it.prompt() == "engine 100%");
}

TEST(interact, closed_hood_hides_bay_parts)
{
    Bay bay;
    CHECK(bay.setup());
    CHECK(bay.sys.hood_open < 0.1f);

    const Ray r = bay.aim_at_socket(PART_BATTERY);
    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() != InteractAction::RemovePart);
}

TEST(interact, picking_up_a_dropped_item)
{
    Bay bay;
    CHECK(bay.setup());

    Item item;
    item.kind = ITEM_ALTERNATOR;
    item.condition = 0.8f;
    const Vec3 spot{4.0f, 0.4f, -2.0f};
    interact_spawn_pickup(bay.entities, bay.world, item, spot, 0.0f, Vec3{});
    CHECK(bay.entities.count() == 1);

    const Vec3 eye = spot + Vec3{0.0f, 1.2f, 1.2f};
    Ray r;
    r.origin = eye;
    r.dir = normalize(spot - eye);

    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() == InteractAction::Pickup);

    bay.tap(r);
    CHECK(bay.it.hands().kind == ITEM_ALTERNATOR);
    CHECK_NEAR(bay.it.hands().condition, 0.8f, 1e-4);
    CHECK(bay.entities.count() == 0);
}

TEST(interact, a_key_pickup_sets_the_key_flag)
{
    Bay bay;
    CHECK(bay.setup());

    Item item;
    item.kind = ITEM_KEY;
    const Vec3 spot{4.0f, 0.4f, -2.0f};
    interact_spawn_pickup(bay.entities, bay.world, item, spot, 0.0f, Vec3{});

    const Vec3 eye = spot + Vec3{0.0f, 1.2f, 1.2f};
    Ray r;
    r.origin = eye;
    r.dir = normalize(spot - eye);

    CHECK(!bay.it.has_key());
    bay.tap(r);
    CHECK(bay.it.has_key());
    CHECK(bay.it.hands().kind == ITEM_NONE);
}

TEST(interact, dropping_spawns_a_pickup_again)
{
    Bay bay;
    CHECK(bay.setup());
    bay.it.hands().kind = ITEM_RADIATOR;
    bay.it.hands().condition = 0.6f;

    CHECK(bay.it.drop(bay.entities, bay.world, Vec3{4.0f, 1.2f, 0.0f}, Vec3{0.0f, 0.0f, -1.0f},
                      0.5f));
    CHECK(bay.it.hands().kind == ITEM_NONE);
    CHECK(bay.entities.count() == 1);

    CHECK(!bay.it.drop(bay.entities, bay.world, Vec3{4.0f, 1.2f, 0.0f},
                       Vec3{0.0f, 0.0f, -1.0f}, 0.5f));
}

TEST(interact, aiming_at_nothing_reports_no_action)
{
    Bay bay;
    CHECK(bay.setup());
    Ray r;
    r.origin = Vec3{40.0f, 2.0f, 40.0f};
    r.dir = Vec3{0.0f, 1.0f, 0.0f};

    bay.it.update(bay.boxes, bay.ctx(r, false, false), kDt);
    CHECK(bay.it.action() == InteractAction::None);
    CHECK(bay.it.prompt().empty());
}

TEST(interact, zone_pickups_spawn_on_the_ground)
{
    Bay bay;
    CHECK(bay.setup());

    Terrain terrain;
    terrain.heightfield().alloc(bay.arena, 32, 2.0f);
    terrain.heightfield().recompute_extents();

    ZonePickups pickups;
    pickups.count = 2;
    pickups.items[0].item.assign("battery");
    pickups.items[0].x = 10.0f;
    pickups.items[0].z = 10.0f;
    pickups.items[0].condition = 0.9f;
    pickups.items[1].item.assign("not_a_thing");
    pickups.items[1].x = 12.0f;
    pickups.items[1].z = 10.0f;

    interact_spawn_zone_pickups(bay.entities, bay.world, terrain, pickups);

    CHECK(bay.entities.count() == 1);
}

TEST(interact, boxes_fall_back_to_defaults_when_missing)
{
    Arena arena(megabytes(4));
    InteractBoxes boxes;
    boxes.init(arena, "assets/cars/definitely_missing.cfg");
    CHECK(boxes.box(IBOX_DOOR).name == "door");
    CHECK_NEAR(boxes.box(IBOX_DOOR).center.x, 0.78f, 1e-5);
    CHECK_NEAR(boxes.box(IBOX_FUEL).half.z, 0.14f, 1e-5);
}
