#include "test.h"

#include "core/arena.h"
#include "math/glm_bridge.h"
#include "net/session.h"
#include "sim/sim.h"
#include "world/scenes.h"
#include "world/destination.h"

#include <memory>
#include <variant>
#include <vector>

using namespace anom;

namespace {
constexpr const char* kZoneDir = "assets/zones/testzone";

struct Machine {
    Arena perm{megabytes(256)};
    Arena scratch{megabytes(256)};
    std::unique_ptr<Sim> sim = std::make_unique<Sim>();
    NetSession net;
    std::vector<SlotCommand> commands;
    ghost::game::EventList seen;

    bool setup() { return sim->init(perm, scratch, kZoneDir); }

    void step(const PlayerCommand& cmd)
    {
        net.receive(*sim);
        net.commands(*sim, cmd, commands);
        sim->tick(commands, kFixedDt);
        net.afterTick(*sim, cmd);
        seen.insert(seen.end(), sim->events().begin(), sim->events().end());
        sim->events().clear();
        net.eventsConsumed();
    }

    PlayerId me() const { return sim->local_id(); }

    PlayerCommand look_at(Vec3 target) const
    {
        const Player& p = sim->player(me());
        PlayerCommand cmd;
        cmd.gameplay = true;
        cmd.view_origin = p.pos() + p.up() * kPlayerEyeHeight;
        cmd.view_dir = normalize(target - cmd.view_origin);
        return cmd;
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
};

struct Pair {
    Machine host;
    Machine client;
    ghost::engine::LoopbackTransport* host_link = nullptr;
    ghost::engine::LoopbackTransport* client_link = nullptr;

    bool setup()
    {
        if (!host.setup() || !client.setup()) {
            return false;
        }
        auto h = ghost::engine::loopbackHost();
        auto c = ghost::engine::loopbackClient(*h);
        host_link = h.get();
        client_link = c.get();
        host.net.attach(std::move(h), NetSession::Role::Host, *host.sim, "Host");
        client.net.attach(std::move(c), NetSession::Role::Client, *client.sim, "Guest");
        run(PlayerCommand{}, PlayerCommand{}, 8);
        return client.net.welcomed();
    }

    void run(const PlayerCommand& host_cmd, const PlayerCommand& client_cmd, u32 ticks)
    {
        for (u32 i = 0; i < ticks; i++) {
            host.step(host_cmd);
            client.step(client_cmd);
        }
    }

    PlayerCommand idle(const Machine& m) const
    {
        PlayerCommand cmd;
        cmd.gameplay = true;
        const Player& p = m.sim->player(m.me());
        cmd.view_origin = p.pos() + p.up() * kPlayerEyeHeight;
        cmd.view_dir = Vec3{0.0f, 0.0f, -1.0f};
        return cmd;
    }
};

Vec3 driver_door(const Sim& sim)
{
    const RigidBody* car = sim.phys().body(sim.vehicle().body());
    const Vec3 box = const_cast<Sim&>(sim).boxes().box(IBOX_DOOR).center;
    return car->pos + rotate(car->rot, Vec3{-f_abs(box.x), box.y, box.z} - sim.vehicle().config().com_offset);
}

Vec3 beside_door(const Sim& sim)
{
    const RigidBody* car = sim.phys().body(sim.vehicle().body());
    const Vec3 side = rotate(car->rot, Vec3{-(car->half_extents.x + 0.6f), 0.0f, 0.0f});
    const Vec3 door = car->pos + side;
    return Vec3{door.x, car->pos.y - car->half_extents.y, door.z};
}

}

TEST(net, the_protocol_version_covers_both_games)
{
    CHECK(net::protocolVersion() != ghost::game::net::protocolVersion());
    CHECK(net::protocolVersion() != 0u);
}

TEST(net, edges_travel_reliably_and_the_rest_continuously)
{
    PlayerCommand cmd;
    cmd.use_down = true;
    cmd.throttle = 0.7f;
    CHECK(!net::hasEdges(cmd));
    cmd.use_pressed = true;
    CHECK(net::hasEdges(cmd));
    const PlayerCommand steady = net::continuousPart(cmd);
    CHECK(!steady.use_pressed);
    CHECK(steady.use_down);
    CHECK_NEAR(steady.throttle, 0.7f, 1e-6);

    PlayerCommand merged = steady;
    net::mergeEdges(merged, cmd);
    CHECK(merged.use_pressed);
    CHECK_NEAR(merged.throttle, 0.7f, 1e-6);

    PlayerCommand car;
    car.horn = true;
    CHECK(!net::hasEdges(car));
    car.handbrake_toggle = true;
    CHECK(net::hasEdges(car));
    car.handbrake_toggle = false;
    car.ignition_tap = true;
    CHECK(net::hasEdges(car));
    car.wipers_cycle = true;
    const PlayerCommand car_steady = net::continuousPart(car);
    CHECK(!car_steady.ignition_tap);
    CHECK(!car_steady.wipers_cycle);
    CHECK(car_steady.horn);
    PlayerCommand car_merged = car_steady;
    net::mergeEdges(car_merged, car);
    CHECK(car_merged.ignition_tap);
    CHECK(car_merged.wipers_cycle);
}

TEST(net, a_world_snapshot_survives_the_wire)
{
    Machine m;
    if (!m.setup()) {
        FAIL("sim init");
        return;
    }
    m.sim->add_player("Second");
    WorldSnapshot out;
    m.sim->make_snapshot(out);
    const std::vector<std::uint8_t> bytes = net::encodeWorld(out);
    ghost::game::net::Reader reader(bytes);
    reader.type();
    const auto back = net::decodeWorld(reader);
    CHECK(back.has_value());
    if (!back) {
        return;
    }
    CHECK(back->players.size() == 2);
    CHECK(back->pickups.size() == out.pickups.size());
    CHECK(back->pickups.size() > 0);
    CHECK(bytes.size() < 64u * 1024u);
}

TEST(net, a_client_joins_and_each_side_sees_the_other)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    CHECK(pair.client.me() == 1);
    CHECK(pair.client.sim->role() == SimRole::Client);
    CHECK(pair.host.sim->slot(1) != nullptr);
    CHECK(pair.host.sim->slot(1)->remote);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);
    const PlayerSlot* host_seen = pair.client.sim->slot(0);
    CHECK(host_seen != nullptr);
    if (host_seen) {
        CHECK(host_seen->remote);
        CHECK(length(host_seen->player.pos() - pair.host.sim->player(0).pos()) < 0.05f);
    }
}

TEST(net, a_joined_client_carries_a_loaded_gun_and_a_full_pouch)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);
    const PlayerSlot* mine = pair.client.sim->slot(pair.client.me());
    const PlayerSlot* hosts = pair.host.sim->slot(0);
    CHECK(mine != nullptr);
    CHECK(hosts != nullptr);
    if (!mine || !hosts) {
        return;
    }
    CHECK(mine->gun.pouch.total() == hosts->gun.pouch.total());
    CHECK(mine->gun.pouch.total() > 0);
    int live = 0;
    for (const ghost::game::Chamber& chamber : mine->gun.mechanism.state().chambers) {
        live += chamber.state == ghost::game::ChamberState::Live ? 1 : 0;
    }
    CHECK(live == ghost::game::kChamberCount);
}

TEST(net, the_client_follows_the_host_into_an_arena_and_back)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    Sim& csim = *pair.client.sim;
    PlayerCommand go = pair.idle(pair.host);
    go.scene = scene_index("yard");
    pair.run(go, pair.idle(pair.client), 1);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 40);
    CHECK(csim.zone_dir() == std::string_view("assets/zones/yard"));
    CHECK(csim.scene().kind == SceneKind::Arena);
    CHECK(csim.player_rules().arena);
    const Entity* spawn = csim.spawn_entity_for(pair.client.me());
    CHECK(spawn != nullptr);
    if (spawn) {
        const Vec3 feet = csim.player(pair.client.me()).pos();
        CHECK(length(Vec3{feet.x - spawn->pos.x, 0.0f, feet.z - spawn->pos.z}) < 0.5f);
    }
    CHECK(csim.slot(pair.client.me())->gun.pouch.count(ghost::game::kPlainElement) == 30);
    CHECK(length(hsim.player(pair.client.me()).pos() - csim.player(pair.client.me()).pos()) < 0.5f);

    go.scene = scene_index("testzone");
    pair.run(go, pair.idle(pair.client), 1);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 40);
    CHECK(csim.zone_dir() == std::string_view("assets/zones/testzone"));
    CHECK(!csim.player_rules().arena);
    CHECK(csim.has_car());
}

TEST(net, a_client_joining_a_host_already_in_an_arena_gets_the_arena_kit)
{
    Pair pair;
    if (!pair.host.setup() || !pair.client.setup()) {
        FAIL("sim init");
        return;
    }
    CHECK(pair.host.sim->switch_scene(scene_index("yard")));
    pair.host.step(PlayerCommand{});
    auto h = ghost::engine::loopbackHost();
    auto c = ghost::engine::loopbackClient(*h);
    pair.host.net.attach(std::move(h), NetSession::Role::Host, *pair.host.sim, "Host");
    pair.client.net.attach(std::move(c), NetSession::Role::Client, *pair.client.sim, "Guest");
    pair.run(PlayerCommand{}, PlayerCommand{}, 8);
    CHECK(pair.client.net.welcomed());
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);

    Sim& csim = *pair.client.sim;
    CHECK(csim.player_rules().arena);
    const PlayerSlot* mine = csim.slot(pair.client.me());
    const PlayerSlot* hosts = pair.host.sim->slot(0);
    CHECK(mine != nullptr);
    CHECK(hosts != nullptr);
    if (!mine || !hosts) {
        return;
    }
    CHECK(mine->gun.pouch.count(ghost::game::kPlainElement) == 30);
    CHECK(mine->gun.pouch.total() == hosts->gun.pouch.total());
    CHECK(pair.host.sim->slot(pair.client.me())->gun.pouch.total() == hosts->gun.pouch.total());
}

TEST(net, printed_rounds_a_client_takes_land_in_their_own_pouch)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    Sim& csim = *pair.client.sim;
    const ghost::game::ElementId fire = hsim.gameplay().ammo().element("fire");
    hsim.carsys().synth.tray[fire] = 5;
    const int before = csim.slot(pair.client.me())->gun.pouch.count(fire);
    hsim.take_tray(*hsim.slot(pair.client.me()), 0);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 10);
    CHECK(csim.slot(pair.client.me())->gun.pouch.count(fire) == before + 5);
    CHECK(hsim.carsys().synth.tray_total() == 0u);
    CHECK(csim.carsys().synth.tray_total() == 0u);
}

TEST(net, the_host_drives_and_the_client_rides_along)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& csim = *pair.client.sim;
    Sim& hsim = *pair.host.sim;
    hsim.carsys().parts[PART_COMPUTER].installed = false;
    for (u32 side = 0; side < 2; side++) {
        hsim.carsys().door_target[side] = true;
        hsim.carsys().door_open[side] = 1.0f;
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 120);
    const RigidBody* car = hsim.phys().body(hsim.vehicle().body());
    const Vec3 box = hsim.boxes().box(IBOX_DOOR).center;
    const Vec3 com = hsim.vehicle().config().com_offset;
    const auto doorway = [&](f32 sign) {
        return car->pos + rotate(car->rot, Vec3{sign * f_abs(box.x), box.y, box.z + 0.3f} - com);
    };
    const Vec3 right_side = rotate(car->rot, Vec3{car->half_extents.x + 0.6f, 0.0f, 0.0f});
    hsim.player(0).init(beside_door(hsim), 0.0f);
    csim.player(1).init(Vec3{car->pos.x + right_side.x, car->pos.y - car->half_extents.y, car->pos.z + right_side.z}, 0.0f);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 20);
    for (u32 i = 0; i < 240 && (hsim.driver() != 0 || csim.player(1).state() != PlayerState::Driving); i++) {
        PlayerCommand drive = pair.host.look_at(doorway(-1.0f));
        drive.use_pressed = hsim.interact(0).action() == InteractAction::EnterCar;
        drive.use_down = drive.use_pressed;
        PlayerCommand ride = pair.client.look_at(doorway(1.0f));
        ride.use_pressed = csim.interact(1).action() == InteractAction::EnterCar;
        ride.use_down = ride.use_pressed;
        pair.run(drive, ride, 1);
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 60);
    CHECK(hsim.driver() == 0);
    CHECK(csim.driver() == 0);
    CHECK(csim.player(1).state() == PlayerState::Driving);
    CHECK(csim.player(1).seat() == 1u);
    CHECK(hsim.player(1).state() == PlayerState::Driving);
    CHECK(hsim.player(1).seat() == 1u);
    CHECK(hsim.seat_owner(1) == 1);
}

TEST(net, a_wrong_version_is_refused)
{
    Machine host;
    if (!host.setup()) {
        FAIL("sim init");
        return;
    }
    auto h = ghost::engine::loopbackHost();
    auto raw = ghost::engine::loopbackClient(*h);
    host.net.attach(std::move(h), NetSession::Role::Host, *host.sim, "Host");
    raw->send(1, ghost::game::net::encode(ghost::game::net::Hello{12345u, "Old"}), true);
    host.step(PlayerCommand{});
    bool refused = false;
    ghost::engine::NetEvent event;
    while (raw->poll(event)) {
        if (event.type == ghost::engine::NetEvent::Type::Message && !event.data.empty()) {
            refused = refused || event.data[0] == static_cast<std::uint8_t>(ghost::game::net::Msg::Refused);
        }
    }
    CHECK(refused);
    CHECK(host.sim->slot(1) == nullptr);
}

TEST(net, the_clients_own_body_moves_on_the_host)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& csim = *pair.client.sim;
    const Vec3 start = csim.player(1).pos();
    const Vec3 away = normalize(Vec3{start.x - csim.phys().body(csim.vehicle().body())->pos.x, 0.0f,
                                     start.z - csim.phys().body(csim.vehicle().body())->pos.z});
    for (u32 i = 0; i < 120; i++) {
        PlayerCommand walk = pair.client.look_at(csim.player(1).pos() + away * 10.0f + Vec3{0.0f, 1.4f, 0.0f});
        walk.face_view = true;
        walk.move_z = 1.0f;
        pair.run(pair.idle(pair.host), walk, 1);
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 6);
    const Vec3 moved = csim.player(1).pos();
    CHECK(length(moved - start) > 2.0f);
    CHECK(length(pair.host.sim->player(1).pos() - moved) < 0.1f);
}

TEST(net, a_drive_on_the_host_is_matched_on_the_client)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    hsim.player(0).init(beside_door(hsim), 0.0f);
    hsim.carsys().door_target[0] = true;
    hsim.carsys().door_open[0] = 1.0f;
    for (u32 i = 0; i < 240 && hsim.driver() != 0; i++) {
        PlayerCommand enter = pair.host.look_at(driver_door(hsim));
        enter.use_down = true;
        enter.use_pressed = i % 4 == 0;
        pair.run(enter, pair.idle(pair.client), 1);
    }
    CHECK(hsim.driver() == 0);
    hsim.carsys().key_inserted = true;
    for (u32 i = 0; i < 600 && !hsim.carsys().engine_on; i++) {
        PlayerCommand crank = pair.idle(pair.host);
        crank.crank = true;
        pair.run(crank, pair.idle(pair.client), 1);
    }
    CHECK(hsim.carsys().engine_on);
    hsim.carsys().handbrake_latched = false;
    PlayerCommand drive = pair.idle(pair.host);
    drive.throttle = 1.0f;
    drive.steer = 0.3f;
    pair.run(drive, pair.idle(pair.client), 360);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 8);

    const RigidBody* hcar = hsim.phys().body(hsim.vehicle().body());
    const RigidBody* ccar = pair.client.sim->phys().body(pair.client.sim->vehicle().body());
    CHECK(pair.client.sim->driver() == 0);
    CHECK(length(hcar->pos - ccar->pos) < 0.6f);
    CHECK(length(hcar->pos - hsim.spawn().car_pos) > 2.0f);
}

TEST(net, a_door_opened_by_the_client_is_opened_once_by_the_host)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& csim = *pair.client.sim;
    Sim& hsim = *pair.host.sim;
    const bool before = hsim.carsys().door_target[0];
    csim.player(1).init(beside_door(csim), 0.0f);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);

    PlayerCommand press = pair.client.look_at(driver_door(csim));
    press.use_down = true;
    press.use_pressed = true;
    pair.run(pair.idle(pair.host), press, 1);
    PlayerCommand hold = pair.client.look_at(driver_door(csim));
    pair.run(pair.idle(pair.host), hold, 40);

    CHECK(hsim.carsys().door_target[0] != before);
    CHECK(pair.host.count<ghost::game::DoorMoved>() == 1);
    CHECK(csim.carsys().door_target[0] == hsim.carsys().door_target[0]);
    CHECK(pair.client.count<ghost::game::DoorMoved>() == 1);
}

TEST(net, a_pickup_taken_by_the_client_goes_into_their_hands_on_both)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    Sim& csim = *pair.client.sim;
    const Entity* item = hsim.find_pickup(ITEM_BATTERY);
    if (!item) {
        item = hsim.find_pickup(ITEM_JERRYCAN);
    }
    if (!item) {
        FAIL("no pickup in the zone");
        return;
    }
    const ItemKind kind = static_cast<ItemKind>(item->aux_kind);
    const Vec3 at = item->pos;
    csim.player(1).init(at + Vec3{1.2f, 0.4f, 0.0f}, 0.0f);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 30);
    for (u32 i = 0; i < 400 && hsim.interact(1).hands().kind == ITEM_NONE; i++) {
        PlayerCommand grab = pair.client.look_at(at);
        grab.use_down = true;
        grab.use_pressed = i == 0;
        pair.run(pair.idle(pair.host), grab, 1);
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 8);
    CHECK(hsim.interact(1).hands().kind == kind);
    CHECK(csim.interact(1).hands().kind == kind);
    CHECK(hsim.interact(0).hands().kind == ITEM_NONE);
}

TEST(net, a_client_with_the_gun_out_holsters_and_takes_a_pickup)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    Sim& csim = *pair.client.sim;
    const Entity* item = hsim.find_pickup(ITEM_BATTERY);
    if (!item) {
        item = hsim.find_pickup(ITEM_JERRYCAN);
    }
    if (!item) {
        FAIL("no pickup in the zone");
        return;
    }
    const ItemKind kind = static_cast<ItemKind>(item->aux_kind);
    const Vec3 at = item->pos;
    csim.player(1).init(at + Vec3{1.2f, 0.4f, 0.0f}, 0.0f);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 30);
    PlayerCommand draw = pair.client.look_at(at);
    draw.holster = true;
    pair.run(pair.idle(pair.host), draw, 1);
    pair.run(pair.idle(pair.host), pair.client.look_at(at), 80);
    CHECK(!csim.player(1).movement().state().holstered);
    for (u32 i = 0; i < 400 && hsim.interact(1).hands().kind == ITEM_NONE; i++) {
        PlayerCommand grab = pair.client.look_at(at);
        grab.use_down = true;
        grab.use_pressed = i == 0;
        pair.run(pair.idle(pair.host), grab, 1);
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 8);
    CHECK(hsim.interact(1).hands().kind == kind);
    CHECK(csim.interact(1).hands().kind == kind);
    CHECK(csim.player(1).movement().state().holstered);
}

TEST(net, the_client_drives_and_the_host_follows)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& csim = *pair.client.sim;
    Sim& hsim = *pair.host.sim;
    hsim.carsys().door_target[0] = true;
    hsim.carsys().door_open[0] = 1.0f;
    csim.player(1).init(beside_door(csim), 0.0f);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);
    for (u32 i = 0; i < 240 && csim.driver() != 1; i++) {
        PlayerCommand enter = pair.client.look_at(driver_door(csim));
        enter.use_down = true;
        enter.use_pressed = i % 4 == 0;
        pair.run(pair.idle(pair.host), enter, 1);
    }
    CHECK(csim.driver() == 1);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 120);
    CHECK(hsim.driver() == 1);

    hsim.carsys().key_inserted = true;
    for (u32 i = 0; i < 600 && !hsim.carsys().engine_on; i++) {
        PlayerCommand crank = pair.idle(pair.client);
        crank.crank = true;
        pair.run(pair.idle(pair.host), crank, 1);
    }
    CHECK(hsim.carsys().engine_on);
    hsim.carsys().handbrake_latched = false;
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 6);
    PlayerCommand drive = pair.idle(pair.client);
    drive.throttle = 1.0f;
    pair.run(pair.idle(pair.host), drive, 360);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 4);

    const RigidBody* hcar = hsim.phys().body(hsim.vehicle().body());
    const RigidBody* ccar = csim.phys().body(csim.vehicle().body());
    CHECK(length(ccar->pos - hsim.spawn().car_pos) > 2.0f);
    CHECK(length(hcar->pos - ccar->pos) < 0.6f);
}

TEST(net, a_photo_from_the_client_is_kept_by_the_host_and_named)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    std::vector<u8> frame(kPhotoBytes, 90);
    CHECK(pair.client.net.sendPhoto(frame));
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 4);
    bool named = false;
    for (const ghost::game::GameEvent& e : pair.client.seen) {
        if (const auto* photo = std::get_if<ghost::game::PhotoTaken>(&e)) {
            named = named || photo->player == 1;
        }
    }
    CHECK(named);
}

TEST(net, travel_takes_every_player_to_the_new_zone)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    Sim& csim = *pair.client.sim;
    hsim.carsys().parts[PART_COIL].installed = true;
    const i32 touge = destination_index("touge");
    hsim.set_travel_charge(1.0f);
    hsim.arm_travel(touge);
    for (u32 i = 0; i < 480 && pair.client.count<ghost::game::TravelArrived>() == 0; i++) {
        pair.run(pair.idle(pair.host), pair.idle(pair.client), 1);
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 30);
    CHECK(hsim.zone_dir() == destinations()[static_cast<u32>(touge)].zone_dir);
    CHECK(csim.zone_dir() == hsim.zone_dir());
    CHECK(length(csim.player(1).pos() - hsim.player(1).pos()) < 0.5f);
    CHECK(length(csim.player(1).pos() - hsim.spawn().car_pos) < 6.0f);
}

TEST(net, a_client_that_leaves_is_gone_from_the_host)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    pair.client.net.leave(*pair.client.sim, "bye");
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 4);
    CHECK(pair.host.sim->slot(1) == nullptr);
    CHECK(pair.client.sim->role() == SimRole::Solo);
    CHECK(pair.client.me() == 0);
}

TEST(net, the_terminal_screen_is_mirrored_to_the_client)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    hsim.carsys().parts[PART_COMPUTER].installed = true;
    hsim.carsys().computer_on = true;
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 120);
    CHECK(hsim.terminal().powered());
    CHECK(pair.client.sim->terminal().powered());
    bool same = true;
    for (u32 r = 0; r < kTermRows; r++) {
        for (u32 c = 0; c < kTermCols; c++) {
            same = same && pair.client.sim->terminal().screen().glyph(r, c) == hsim.terminal().screen().glyph(r, c);
        }
    }
    CHECK(same);
}

TEST(net, a_client_shoots_a_ghost_and_the_host_kills_it)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    Sim& csim = *pair.client.sim;
    i32 wisp = -1;
    for (size_t i = 0; i < hsim.gameplay().ghostData().types.size(); i++) {
        if (hsim.gameplay().ghostData().types[i].name == "wisp") {
            wisp = static_cast<i32>(i);
        }
    }
    hsim.gameplay().clearWorld();
    PlayerCommand spawn = pair.idle(pair.host);
    spawn.spawn_ghost = wisp;
    pair.run(spawn, pair.idle(pair.client), 1);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);
    CHECK(csim.gameplay().ghosts().ghosts().size() == 1);

    PlayerCommand draw = pair.idle(pair.client);
    draw.holster = true;
    pair.run(pair.idle(pair.host), draw, 1);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 72);
    for (i32 shot = 0; shot < 12 && !hsim.gameplay().ghosts().ghosts().empty(); shot++) {
        const Vec3 at = from_glm(hsim.gameplay().ghosts().ghosts().front().position);
        const Player& me = csim.player(1);
        const Vec3 eye = me.pos() + me.up() * me.movement().state().eye_height;
        PlayerCommand fire = pair.client.look_at(at);
        fire.face_view = true;
        fire.barrel_dir = normalize(at - eye);
        fire.muzzle = eye + fire.barrel_dir * 0.6f;
        fire.trigger = true;
        pair.run(pair.idle(pair.host), fire, 40);
        fire.trigger = false;
        pair.run(pair.idle(pair.host), fire, 20);
        csim.slot(1)->gun.mechanism.loadAll(ghost::game::Round{});
    }
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 12);
    CHECK(pair.host.count<ghost::game::GhostDied>() == 1);
    CHECK(pair.client.count<ghost::game::GhostDied>() == 1);
    CHECK(csim.gameplay().ghosts().ghosts().empty());
    CHECK(pair.client.count<ghost::game::ShotFired>() >= 1);
}

TEST(net, the_hosts_dummy_hurts_a_client)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    Sim& hsim = *pair.host.sim;
    const Vec3 far = hsim.player(0).pos() + Vec3{30.0f, 0.0f, 30.0f};
    hsim.player(0).init(Vec3{far.x, hsim.terrain().heightfield().sample(far.x, far.z) + 0.5f, far.z}, 0.0f);
    PlayerCommand shoot = pair.idle(pair.host);
    shoot.dummy_script = static_cast<i32>(DummyScript::Shoot);
    pair.run(shoot, pair.idle(pair.client), 1);
    PlayerSlot* dummy = hsim.slot(2);
    CHECK(dummy != nullptr);
    if (!dummy) {
        return;
    }
    const Vec3 near = pair.client.sim->player(1).pos() + Vec3{0.0f, 0.0f, 4.0f};
    dummy->player.init(near, 0.0f);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 900);
    const ghost::game::RosterEntry* hurt = pair.client.sim->roster().find(1);
    CHECK(hurt && hurt->health < 1.0f);
}

TEST(net, big_messages_are_packed_small_and_come_back_exactly)
{
    Machine m;
    if (!m.setup()) {
        FAIL("sim init");
        return;
    }
    m.sim->add_player("Second");
    WorldSnapshot out;
    m.sim->make_snapshot(out);
    const std::vector<std::uint8_t> raw = net::encodeWorld(out);
    const std::vector<std::uint8_t> packed = net::pack(raw);
    CHECK(packed.size() * 3 < raw.size());
    const auto back = net::unpack(packed);
    CHECK(back.has_value() && *back == raw);
    TermMirror mirror;
    m.sim->terminal().mirror(mirror);
    const std::vector<std::uint8_t> screen = net::encodeScreen(mirror);
    CHECK(net::pack(screen).size() * 2 < screen.size());
    std::vector<std::uint8_t> broken = packed;
    broken.resize(broken.size() / 2);
    CHECK(!net::unpack(broken).has_value());
    std::vector<std::uint8_t> lying = packed;
    lying[4] = 0x7F;
    CHECK(!net::unpack(lying).has_value());
}

TEST(net, a_snapshot_older_than_the_last_one_is_ignored)
{
    Machine m;
    if (!m.setup()) {
        FAIL("sim init");
        return;
    }
    WorldSnapshot base;
    m.sim->make_snapshot(base);
    const auto at = [&](u32 tick) {
        WorldSnapshot s = base;
        s.header.tick = tick;
        return s;
    };
    CHECK(m.sim->queue_snapshot(at(500)));
    CHECK(!m.sim->queue_snapshot(at(480)));
    CHECK(!m.sim->queue_snapshot(at(500)));
    CHECK(m.sim->queue_snapshot(at(504)));
    CHECK(m.sim->queue_snapshot(at(504 + kStaleSnapshotTicks + 10)));
    WorldSnapshot moved = at(3);
    moved.header.zone_serial = base.header.zone_serial + 1;
    CHECK(m.sim->queue_snapshot(std::move(moved)));
}

TEST(net, the_host_dresses_a_client_as_a_cowboy_and_a_client_cannot)
{
    Pair pair;
    if (!pair.setup()) {
        FAIL("join");
        return;
    }
    PlayerCommand dress = pair.idle(pair.host);
    dress.cowboy_target = static_cast<i32>(pair.client.me());
    dress.cowboy_mode = 1;
    pair.run(dress, pair.idle(pair.client), 1);
    pair.run(pair.idle(pair.host), pair.idle(pair.client), 20);
    CHECK(pair.host.sim->wears_cowboy(pair.client.me()));
    CHECK(pair.client.sim->wears_cowboy(pair.client.me()));
    CHECK(!pair.client.sim->wears_cowboy(0));

    PlayerCommand sneaky = pair.idle(pair.client);
    sneaky.cowboy_target = 0;
    sneaky.cowboy_mode = 1;
    pair.run(pair.idle(pair.host), sneaky, 20);
    CHECK(!pair.host.sim->wears_cowboy(0));
    CHECK(!pair.client.sim->wears_cowboy(0));
}
