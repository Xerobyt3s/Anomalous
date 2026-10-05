#include "test.h"

#include "core/arena.h"
#include "math/glm_bridge.h"
#include "net/session.h"
#include "sim/sim.h"
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
