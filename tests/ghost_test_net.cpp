#include "engine/net/transport.h"
#include "game/net/protocol.h"

#include <doctest/doctest.h>

#include <variant>

using namespace ghost;
using namespace ghost::game;

namespace {
std::vector<engine::NetEvent> drain(engine::Transport& transport) {
    std::vector<engine::NetEvent> events;
    engine::NetEvent event;
    while (transport.poll(event)) {
        events.push_back(event);
    }
    return events;
}

}

TEST_CASE("Loopback transports carry messages both ways and report who connects and leaves") {
    auto host = engine::loopbackHost();
    auto alice = engine::loopbackClient(*host);
    auto bob = engine::loopbackClient(*host);

    const auto joined = drain(*host);
    REQUIRE(joined.size() == 2);
    CHECK(joined[0].type == engine::NetEvent::Type::Connected);
    const engine::PeerId alicePeer = joined[0].peer;
    const engine::PeerId bobPeer = joined[1].peer;
    CHECK(alicePeer != bobPeer);
    const auto aliceSees = drain(*alice);
    REQUIRE(aliceSees.size() == 1);
    const engine::PeerId hostPeer = aliceSees[0].peer;
    drain(*bob);

    const std::vector<std::uint8_t> hello{1, 2, 3};
    alice->send(hostPeer, hello, true);
    const auto got = drain(*host);
    REQUIRE(got.size() == 1);
    CHECK(got[0].peer == alicePeer);
    CHECK(got[0].data == hello);

    host->broadcast(hello, true);
    CHECK(drain(*alice).size() == 1);
    CHECK(drain(*bob).size() == 1);
    host->send(bobPeer, hello, true);
    CHECK(drain(*alice).empty());
    CHECK(drain(*bob).size() == 1);

    host->dropUnreliable = true;
    host->broadcast(hello, false);
    CHECK(drain(*alice).empty());
    host->broadcast(hello, true);
    CHECK(drain(*alice).size() == 1);
    drain(*bob);

    host->disconnect(alicePeer);
    const auto gone = drain(*alice);
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].type == engine::NetEvent::Type::Disconnected);
    host->broadcast(hello, true);
    CHECK(drain(*alice).empty());
    CHECK(drain(*bob).size() == 1);
}

TEST_CASE("Every kind of game event survives the wire") {
    EventList sent;
    for (std::size_t i = 0; i < std::variant_size_v<GameEvent>; ++i) {
        const std::size_t before = sent.size();

        std::vector<std::uint8_t> zero(1 + 2 + 1 + 512, 0);
        zero[0] = static_cast<std::uint8_t>(net::Msg::Events);
        zero[1] = 1;
        zero[3] = static_cast<std::uint8_t>(i);
        net::Reader reader(zero);
        reader.type();
        const auto one = net::decodeEvents(reader);
        REQUIRE(one);
        REQUIRE(one->size() == 1);
        sent.push_back(one->front());
        CHECK(sent[before].index() == i);
    }
    ShotFired shot;
    shot.round.element = 3;
    shot.chamber = 4;
    shot.player = 2;
    sent.push_back(shot);
    ProjectileImpact impact;
    impact.point = {1.0f, 2.0f, 3.0f};
    impact.surface = Surface::Player;
    impact.head = true;
    impact.shooter = 1;
    sent.push_back(impact);
    sent.push_back(PlayerDamaged{0.35f, {4.0f, 5.0f, 6.0f}, 3});
    sent.push_back(GhostHurt{7, {0.0f, 1.0f, 0.0f}, 12.5f, DamageKind::Wind, true, 0.14f});

    const std::vector<std::uint8_t> bytes = net::encode(sent);
    net::Reader reader(bytes);
    CHECK(reader.type() == net::Msg::Events);
    const auto got = net::decodeEvents(reader);
    REQUIRE(got);
    REQUIRE(got->size() == sent.size());
    for (std::size_t i = 0; i < sent.size(); ++i) {
        CHECK((*got)[i].index() == sent[i].index());
    }
    const auto& gotShot = std::get<ShotFired>((*got)[sent.size() - 4]);
    CHECK(gotShot.round.element == 3);
    CHECK(gotShot.chamber == 4);
    CHECK(gotShot.player == 2);
    const auto& gotImpact = std::get<ProjectileImpact>((*got)[sent.size() - 3]);
    CHECK(gotImpact.point == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(gotImpact.surface == Surface::Player);
    CHECK(gotImpact.head);
    CHECK(std::get<PlayerDamaged>((*got)[sent.size() - 2]).player == 3);
    const auto& gotHurt = std::get<GhostHurt>(got->back());
    CHECK(gotHurt.killed);
    CHECK(gotHurt.hitstop == doctest::Approx(0.14f));
}

TEST_CASE("A snapshot of the world survives the wire") {
    net::Snapshot s;
    s.tick = 1234;
    s.friendlyFire = false;
    net::PlayerWire player;
    player.id = 2;
    std::snprintf(player.name, sizeof(player.name), "%s", "Sam");
    player.state.position = {1.0f, 0.0f, -3.0f};
    player.state.stance = Stance::Slide;
    player.state.height = 1.1f;
    player.gun.crane = 0.75f;
    player.gun.chambers[2] = ChamberState::Spent;
    player.interact = true;
    s.players.push_back(player);
    RosterEntry entry;
    entry.id = 2;
    entry.health = 0.4f;
    entry.downed = true;
    s.roster.push_back(entry);
    Ghost ghost;
    ghost.id = 9;
    ghost.position = {0.0f, 1.5f, -20.0f};
    ghost.state = GhostState::Rush;
    ghost.hitstop = 0.1f;
    s.ghosts.push_back(ghost);
    s.crates.push_back({{1.0f, 2.0f, 3.0f}, glm::quat(0.5f, 0.5f, 0.5f, 0.5f), {0.0f, -1.0f, 0.0f}});
    ElementVolume volume;
    volume.id = 5;
    volume.element = 1;
    volume.radius = 2.5f;
    s.volumes.push_back(volume);
    s.materialDrops.push_back({3, {1.0f, 0.0f, 1.0f}, {}, true, 2.0f});
    s.rounds.push_back({{2.0f, 0.0f, 2.0f}, {}, 1.0f, Round{4}, true});
    s.materials = {5, 0, 2};

    const std::vector<std::uint8_t> bytes = net::encode(s);
    net::Reader reader(bytes);
    CHECK(reader.type() == net::Msg::Snapshot);
    const auto got = net::decodeSnapshot(reader);
    REQUIRE(got);
    CHECK(got->tick == 1234);
    CHECK_FALSE(got->friendlyFire);
    REQUIRE(got->players.size() == 1);
    CHECK(std::string(got->players[0].name) == "Sam");
    CHECK(got->players[0].state.stance == Stance::Slide);
    CHECK(got->players[0].gun.crane == doctest::Approx(0.75f));
    CHECK(got->players[0].gun.chambers[2] == ChamberState::Spent);
    CHECK(got->players[0].interact);
    REQUIRE(got->roster.size() == 1);
    CHECK(got->roster[0].downed);
    REQUIRE(got->ghosts.size() == 1);
    CHECK(got->ghosts[0].state == GhostState::Rush);
    CHECK(got->ghosts[0].position.z == doctest::Approx(-20.0f));
    REQUIRE(got->crates.size() == 1);
    CHECK(got->crates[0].rotation.w == doctest::Approx(0.5f));
    REQUIRE(got->volumes.size() == 1);
    CHECK(got->volumes[0].radius == doctest::Approx(2.5f));
    REQUIRE(got->materialDrops.size() == 1);
    CHECK(got->materialDrops[0].material == 3);
    REQUIRE(got->rounds.size() == 1);
    CHECK(got->rounds[0].round.element == 4);
    CHECK(got->materials == std::vector<std::int32_t>{5, 0, 2});
}

TEST_CASE("Cut-off or foreign packets are refused, not read past their end") {
    net::Snapshot s;
    s.ghosts.resize(3);
    std::vector<std::uint8_t> bytes = net::encode(s);
    bytes.resize(bytes.size() / 2);
    net::Reader cut(bytes);
    cut.type();
    CHECK_FALSE(net::decodeSnapshot(cut));

    std::vector<std::uint8_t> bad{static_cast<std::uint8_t>(net::Msg::Events), 1, 0, 250};
    net::Reader reader(bad);
    reader.type();
    CHECK_FALSE(net::decodeEvents(reader));

    std::vector<std::uint8_t> lying{static_cast<std::uint8_t>(net::Msg::DropRounds), 0xFF, 0xFF, 1, 2, 3};
    net::Reader liar(lying);
    liar.type();
    CHECK(liar.list<net::RoundWire>().empty());
    CHECK_FALSE(liar.ok());

    net::Reader empty(std::span<const std::uint8_t>{});
    CHECK(empty.pod<std::uint32_t>() == 0);
    CHECK_FALSE(empty.ok());
}

TEST_CASE("The handshake carries a version and a name; the small messages carry what they say") {
    const auto bytes = net::encode(net::Hello{net::protocolVersion(), "Robin"});
    net::Reader reader(bytes);
    CHECK(reader.type() == net::Msg::Hello);
    const auto hello = net::decodeHello(reader);
    REQUIRE(hello);
    CHECK(hello->version == net::protocolVersion());
    CHECK(hello->name == "Robin");
    CHECK(net::protocolVersion() != 0);

    net::ShotMsg shot;
    shot.origin = {1.0f, 1.5f, 0.0f};
    shot.spreadDeg = 0.5f;
    shot.round.element = 2;
    const auto shotBytes = net::encode(shot);
    net::Reader shotReader(shotBytes);
    CHECK(shotReader.type() == net::Msg::Shot);
    const auto gotShot = shotReader.pod<net::ShotMsg>();
    CHECK(shotReader.ok());
    CHECK(gotShot.origin == shot.origin);
    CHECK(gotShot.round.element == 2);

    const auto pushBytes = net::encodeImpulse({0.0f, 4.0f, -2.0f});
    net::Reader push(pushBytes);
    CHECK(push.type() == net::Msg::Impulse);
    CHECK(push.pod<glm::vec3>() == glm::vec3(0.0f, 4.0f, -2.0f));

    const auto refusedBytes = net::encodeRefused("The game is full");
    net::Reader refused(refusedBytes);
    CHECK(refused.type() == net::Msg::Refused);
    CHECK(refused.text() == "The game is full");
}
