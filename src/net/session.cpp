#include "net/session.h"
#include "engine/net/steam.h"
#include "core/log.h"
#include "math/glm_bridge.h"
#include "sim/sim.h"

#include <cstring>

namespace anom {
namespace net {
namespace {
using ghost::game::net::Reader;
using ghost::game::net::Writer;

ghost::game::net::Msg tag(Msg msg)
{
    return static_cast<ghost::game::net::Msg>(msg);
}

}

bool ownHandsOnly(const ghost::game::GameEvent& event)
{
    using namespace ghost::game;
    return std::holds_alternative<DryFired>(event) || std::holds_alternative<HammerCocked>(event) ||
           std::holds_alternative<CylinderOpened>(event) || std::holds_alternative<CylinderClosed>(event) ||
           std::holds_alternative<ChambersEjected>(event) || std::holds_alternative<SpeedloaderUsed>(event) ||
           std::holds_alternative<RoundLoaded>(event) || std::holds_alternative<RoundPickedUp>(event) ||
           std::holds_alternative<PlayerHit>(event) || std::holds_alternative<GunHolstered>(event);
}

constexpr std::size_t kPackRun = 128;
constexpr std::uint32_t kPackLimit = 1u << 20;

std::uint32_t protocolVersion()
{
    constexpr std::uint32_t kRevision = 2;
    return ghost::game::net::protocolVersion() * 31u + kRevision * 1000003u
         + static_cast<std::uint32_t>(sizeof(SlotWire) * 7 + sizeof(PickupWire) * 11 + sizeof(WorldHeader) * 13
                                      + sizeof(PlayerCommand) * 17 + sizeof(BodyMsg) * 19 + sizeof(TermMirror) * 23);
}

bool hasEdges(const PlayerCommand& c)
{
    return c.use_pressed || c.throw_power >= 0.0f || c.place_commit || c.stow_cable || c.take_key
        || c.terminal_keys != 0 || c.terminal_char_count > 0 || c.terminal_leave || c.headlights_toggle
        || c.manual_toggle || c.shift != 0 || c.recover || c.reset_car || c.dummy_cycle || c.dummy_script >= 0
        || c.handbrake_toggle || c.ignition_tap || c.wipers_cycle;
}

PlayerCommand continuousPart(const PlayerCommand& c)
{
    PlayerCommand out = c;
    out.use_pressed = false;
    out.throw_power = -1.0f;
    out.place_commit = false;
    out.stow_cable = false;
    out.take_key = false;
    out.terminal_keys = 0;
    out.terminal_char_count = 0;
    out.terminal_leave = false;
    out.headlights_toggle = false;
    out.manual_toggle = false;
    out.shift = 0;
    out.recover = false;
    out.reset_car = false;
    out.dummy_cycle = false;
    out.dummy_script = -1;
    out.handbrake_toggle = false;
    out.ignition_tap = false;
    out.wipers_cycle = false;
    out.jump = false;
    out.crawl = false;
    out.interact = false;
    out.holster = false;
    out.look_dx = 0.0f;
    out.look_dy = 0.0f;
    return out;
}

void mergeEdges(PlayerCommand& into, const PlayerCommand& from)
{
    into.view_origin = from.view_origin;
    into.view_dir = from.view_dir;
    into.use_down = into.use_down || from.use_down;
    into.use_pressed = from.use_pressed;
    into.throw_power = from.throw_power;
    into.place_commit = from.place_commit;
    into.place_pos = from.place_pos;
    into.place_yaw = from.place_yaw;
    into.stow_cable = from.stow_cable;
    into.take_key = from.take_key;
    into.terminal_keys = from.terminal_keys;
    into.terminal_char_count = from.terminal_char_count;
    std::memcpy(into.terminal_chars, from.terminal_chars, sizeof(into.terminal_chars));
    into.terminal_leave = from.terminal_leave;
    into.headlights_toggle = from.headlights_toggle;
    into.manual_toggle = from.manual_toggle;
    into.shift = from.shift;
    into.recover = from.recover;
    into.reset_car = from.reset_car;
    into.dummy_cycle = from.dummy_cycle;
    into.dummy_script = from.dummy_script;
    into.handbrake_toggle = from.handbrake_toggle;
    into.ignition_tap = from.ignition_tap;
    into.wipers_cycle = from.wipers_cycle;
}

std::vector<std::uint8_t> encodeBody(const BodyMsg& body)
{
    Writer w(tag(Msg::Body));
    w.pod(body.body);
    w.pod(body.command);
    w.pod(body.gun);
    w.pod(body.hasCar);
    if (body.hasCar) {
        w.pod(body.car);
    }
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeIntent(const PlayerCommand& command)
{
    Writer w(tag(Msg::Intent));
    w.pod(command);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeWorld(const WorldSnapshot& s)
{
    Writer w(tag(Msg::World));
    w.pod(s.header);
    w.list(s.players);
    w.list(s.roster);
    w.list(s.pickups);
    w.list(s.gameplay.ghosts);
    w.list(s.gameplay.volumes);
    w.list(s.gameplay.materialDrops);
    w.list(s.gameplay.rounds);
    w.list(s.gameplay.materials);
    return std::move(w.bytes);
}

std::optional<WorldSnapshot> decodeWorld(Reader& reader)
{
    WorldSnapshot s;
    s.header = reader.pod<WorldHeader>();
    s.players = reader.list<SlotWire>();
    s.roster = reader.list<ghost::game::RosterEntry>();
    s.pickups = reader.list<PickupWire>();
    s.gameplay.ghosts = reader.list<ghost::game::Ghost>();
    s.gameplay.volumes = reader.list<ghost::game::ElementVolume>();
    s.gameplay.materialDrops = reader.list<ghost::game::net::DropWire>();
    s.gameplay.rounds = reader.list<ghost::game::net::RoundWire>();
    s.gameplay.materials = reader.list<std::int32_t>();
    if (!reader.ok()) {
        return std::nullopt;
    }
    return s;
}

std::vector<std::uint8_t> pack(const std::vector<std::uint8_t>& raw)
{
    std::vector<std::uint8_t> out;
    out.reserve(raw.size() / 3 + 8);
    out.push_back(static_cast<std::uint8_t>(Msg::Packed));
    const auto size = static_cast<std::uint32_t>(raw.size());
    for (int b = 0; b < 4; ++b) {
        out.push_back(static_cast<std::uint8_t>(size >> (8 * b)));
    }
    std::size_t i = 0;
    const std::size_t n = raw.size();
    while (i < n) {
        std::size_t j = i;
        if (raw[i] == 0) {
            while (j < n && raw[j] == 0 && j - i < kPackRun) {
                ++j;
            }
            out.push_back(static_cast<std::uint8_t>(0x80u | (j - i - 1)));
        } else {
            while (j < n && j - i < kPackRun && !(raw[j] == 0 && j + 1 < n && raw[j + 1] == 0)) {
                ++j;
            }
            out.push_back(static_cast<std::uint8_t>(j - i - 1));
            out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(i), raw.begin() + static_cast<std::ptrdiff_t>(j));
        }
        i = j;
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> unpack(std::span<const std::uint8_t> packed)
{
    if (packed.size() < 5 || packed[0] != static_cast<std::uint8_t>(Msg::Packed)) {
        return std::nullopt;
    }
    std::uint32_t size = 0;
    for (int b = 0; b < 4; ++b) {
        size |= static_cast<std::uint32_t>(packed[1 + b]) << (8 * b);
    }
    if (size > kPackLimit) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    out.reserve(size);
    std::size_t i = 5;
    while (i < packed.size()) {
        const std::uint8_t control = packed[i++];
        const std::size_t count = static_cast<std::size_t>(control & 0x7Fu) + 1;
        if (out.size() + count > size) {
            return std::nullopt;
        }
        if (control & 0x80u) {
            out.insert(out.end(), count, std::uint8_t{0});
        } else {
            if (i + count > packed.size()) {
                return std::nullopt;
            }
            out.insert(out.end(), packed.begin() + static_cast<std::ptrdiff_t>(i), packed.begin() + static_cast<std::ptrdiff_t>(i + count));
            i += count;
        }
    }
    if (out.size() != size) {
        return std::nullopt;
    }
    return out;
}

std::vector<std::uint8_t> encodeScreen(const TermMirror& mirror)
{
    Writer w(tag(Msg::Screen));
    w.pod(mirror);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodePhoto(std::span<const u8> rgb)
{
    Writer w(tag(Msg::Photo));
    w.bytes.insert(w.bytes.end(), rgb.begin(), rgb.end());
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeZone(const ZoneMsg& zone)
{
    Writer w(tag(Msg::Zone));
    w.text(zone.dir);
    w.pod(zone.yaw);
    return std::move(w.bytes);
}

}

namespace {
using ghost::engine::NetEvent;
using ghost::game::net::Reader;

}

bool NetSession::host(Sim& sim, std::uint16_t port)
{
    if (m_role != Role::Solo) {
        return false;
    }
    auto transport = ghost::engine::listen(port, kMaxPlayers - 1);
    if (!transport) {
        m_status = "Could not host: UDP port " + std::to_string(port) + " is not free";
        return false;
    }
    attach(std::move(transport), Role::Host, sim, m_name);
    m_status = "Hosting on UDP port " + std::to_string(port);
    log_info("net: %s", m_status.c_str());
    return true;
}

bool NetSession::join(Sim& sim, const std::string& address, std::uint16_t port, const std::string& name)
{
    if (m_role != Role::Solo) {
        return false;
    }
    auto transport = ghost::engine::connect(address, port);
    if (!transport) {
        m_status = "Can't reach " + address;
        return false;
    }
    attach(std::move(transport), Role::Client, sim, name);
    m_status = "Connecting to " + address + "...";
    log_info("net: %s", m_status.c_str());
    return true;
}

bool NetSession::hostSteam(Sim& sim)
{
    if (m_role != Role::Solo) {
        return false;
    }
    auto transport = ghost::engine::steam::listen(kMaxPlayers - 1);
    if (!transport) {
        m_status = "Could not host on Steam (is Steam running?)";
        return false;
    }
    ghost::engine::steam::createLobby(kMaxPlayers, std::to_string(net::protocolVersion()));
    attach(std::move(transport), Role::Host, sim, m_name);
    m_viaSteam = true;
    m_status = "Hosting on Steam: friends can join";
    log_info("net: %s", m_status.c_str());
    return true;
}

void NetSession::joinSteam(Sim& sim, std::uint64_t lobby)
{
    if (m_role != Role::Solo) {
        leave(sim, "Left to join a friend");
    }
    ghost::engine::steam::joinLobby(lobby);
    m_steamJoining = true;
    m_status = "Joining a friend on Steam...";
}

void NetSession::steamUpdate(Sim& sim, float dt, bool menuOpen)
{
    if (!ghost::engine::steam::available()) {
        return;
    }
    if (const auto asked = ghost::engine::steam::takePendingJoin()) {
        joinSteam(sim, *asked);
    }
    if (m_steamJoining) {
        const ghost::engine::steam::JoinState state = ghost::engine::steam::joinState();
        if (state == ghost::engine::steam::JoinState::Ready) {
            m_steamJoining = false;
            if (auto transport = ghost::engine::steam::connectToLobbyOwner()) {
                attach(std::move(transport), Role::Client, sim, m_name);
                m_viaSteam = true;
                m_status = "Connecting to a friend on Steam...";
            } else {
                ghost::engine::steam::leaveLobby();
                m_status = "Could not connect to that game";
            }
        } else if (state != ghost::engine::steam::JoinState::Entering) {
            m_steamJoining = false;
            m_status = "Could not join that game";
        }
    }
    m_friendsRefresh -= dt;
    if (m_role == Role::Solo && menuOpen && m_friendsRefresh <= 0.0f) {
        m_friendsRefresh = 2.0f;
        m_friendGames.clear();
        for (const ghost::engine::steam::FriendGame& game :
             ghost::engine::steam::friendsPlaying(std::to_string(net::protocolVersion()))) {
            m_friendGames.push_back({game.lobby, game.name});
        }
    }
}

void NetSession::attach(std::unique_ptr<ghost::engine::Transport> transport, Role role, Sim& sim,
                        const std::string& name)
{
    m_net = std::move(transport);
    m_role = role;
    setName(name);
    m_welcomed = role == Role::Host;
    m_remotes.clear();
    m_ticks = 0;
    m_told = sim.events().size();
    m_screenWasOn = false;
    sim.set_role(role == Role::Host ? SimRole::Host : SimRole::Solo);
    if (role == Role::Host) {
        if (PlayerSlot* mine = sim.slot(sim.local_id())) {
            mine->name.assign(m_name);
        }
    }
}

void NetSession::leave(Sim& sim, const std::string& why)
{
    if (m_role == Role::Solo) {
        return;
    }
    if (m_role == Role::Host) {
        for (const Remote& r : m_remotes) {
            sim.remove_player(r.id);
        }
        sim.set_role(SimRole::Solo);
    } else if (sim.role() == SimRole::Client) {
        sim.become_solo(sim.local_id());
    }
    if (m_net) {
        if (m_role == Role::Client && m_hostPeer != 0) {
            m_net->disconnect(m_hostPeer);
        }
        for (const Remote& r : m_remotes) {
            m_net->disconnect(r.peer);
        }
        m_net->flush();
    }
    m_net.reset();
    m_remotes.clear();
    if (m_viaSteam) {
        ghost::engine::steam::leaveLobby();
        m_viaSteam = false;
    }
    m_role = Role::Solo;
    m_welcomed = false;
    m_status = why;
    log_info("net: %s", why.c_str());
}

void NetSession::sendTo(PlayerId id, const std::vector<std::uint8_t>& message)
{
    for (const Remote& r : m_remotes) {
        if (r.id == id && m_net) {
            m_net->send(r.peer, message, true);
        }
    }
}

NetSession::Remote* NetSession::remoteByPeer(ghost::engine::PeerId peer)
{
    for (Remote& r : m_remotes) {
        if (r.peer == peer) {
            return &r;
        }
    }
    return nullptr;
}

void NetSession::receive(Sim& sim)
{
    if (!m_net) {
        return;
    }
    NetEvent event;
    while (m_net && m_net->poll(event)) {
        switch (event.type) {
        case NetEvent::Type::Connected:
            if (m_role == Role::Client) {
                m_hostPeer = event.peer;
                m_net->send(m_hostPeer, ghost::game::net::encode(ghost::game::net::Hello{net::protocolVersion(), m_name}),
                            true);
            }
            break;
        case NetEvent::Type::Disconnected:
            if (m_role == Role::Client) {
                leave(sim, m_welcomed ? "The host left the game" : "Could not connect (no answer)");
            } else if (Remote* r = remoteByPeer(event.peer)) {
                log_info("net: player %u left", static_cast<u32>(r->id));
                sim.remove_player(r->id);
                const ghost::engine::PeerId gone = event.peer;
                std::erase_if(m_remotes, [gone](const Remote& x) { return x.peer == gone; });
            }
            break;
        case NetEvent::Type::Message: {
            if (!event.data.empty() && event.data[0] == static_cast<std::uint8_t>(net::Msg::Packed)) {
                auto raw = net::unpack(event.data);
                if (!raw) {
                    break;
                }
                event.data = std::move(*raw);
            }
            Reader reader(event.data);
            if (m_role == Role::Host) {
                hostHandle(sim, event.peer, reader);
            } else if (m_role == Role::Client) {
                clientHandle(sim, reader);
            }
            break;
        }
        }
    }
}

void NetSession::hostHandle(Sim& sim, ghost::engine::PeerId peer, Reader& reader)
{
    const auto type = static_cast<std::uint8_t>(reader.type());
    Remote* sender = remoteByPeer(peer);
    if (type == static_cast<std::uint8_t>(ghost::game::net::Msg::Hello)) {
        const auto hello = ghost::game::net::decodeHello(reader);
        if (!hello || hello->version != net::protocolVersion()) {
            m_net->send(peer, ghost::game::net::encodeRefused("The host is running a different version of the game"), true);
            m_net->flush();
            m_net->disconnect(peer);
            return;
        }
        const PlayerId id = sender ? kNoPlayer : sim.add_player(hello->name.substr(0, kWireNameChars - 1), true);
        if (id == kNoPlayer) {
            m_net->send(peer, ghost::game::net::encodeRefused("The game is full"), true);
            m_net->flush();
            m_net->disconnect(peer);
            return;
        }
        Remote remote;
        remote.peer = peer;
        remote.id = id;
        remote.body = sim.player(id);
        m_remotes.push_back(remote);
        const Player& body = sim.player(id);
        m_net->send(peer, ghost::game::net::encode(ghost::game::net::Welcome{id, to_glm(body.pos()), false, 0}), true);
        m_net->send(peer, net::encodeZone(net::ZoneMsg{std::string(sim.zone_dir()), body.yaw()}), true);
        WorldSnapshot snapshot;
        sim.make_snapshot(snapshot);
        m_net->send(peer, net::pack(net::encodeWorld(snapshot)), true);
        log_info("net: %s joined as player %u", hello->name.c_str(), static_cast<u32>(id));
        return;
    }
    if (!sender) {
        return;
    }
    switch (type) {
    case static_cast<std::uint8_t>(net::Msg::Body): {
        const Player body = reader.pod<Player>();
        const PlayerCommand command = reader.pod<PlayerCommand>();
        const ghost::game::MechanismView gun = reader.pod<ghost::game::MechanismView>();
        const bool hasCar = reader.pod<bool>();
        CarWire car;
        if (hasCar) {
            car = reader.pod<CarWire>();
        }
        if (reader.ok()) {
            sender->body = body;
            sender->freshBody = true;
            sender->gun = gun;
            sender->freshGun = true;
            sender->latest = net::continuousPart(command);
            if (hasCar) {
                sender->car = car;
                sender->freshCar = true;
            }
        }
        break;
    }
    case static_cast<std::uint8_t>(net::Msg::Intent): {
        const PlayerCommand command = reader.pod<PlayerCommand>();
        if (reader.ok() && sender->intents.size() < 64) {
            sender->intents.push_back(command);
        }
        break;
    }
    case static_cast<std::uint8_t>(ghost::game::net::Msg::Shot): {
        const auto shot = reader.pod<ghost::game::net::ShotMsg>();
        if (reader.ok()) {
            sim.queue_remote_shot(sender->id, shot);
        }
        break;
    }
    case static_cast<std::uint8_t>(ghost::game::net::Msg::DropRounds): {
        auto rounds = reader.list<ghost::game::net::RoundWire>();
        if (reader.ok() && rounds.size() <= 16) {
            sim.queue_remote_rounds(std::move(rounds));
        }
        break;
    }
    case static_cast<std::uint8_t>(net::Msg::Photo): {
        std::vector<u8> rgb(kPhotoBytes);
        for (u32 i = 0; i < kPhotoBytes; i++) {
            rgb[i] = reader.pod<u8>();
        }
        if (reader.ok()) {
            sim.queue_photo(rgb.data(), sender->id);
        }
        break;
    }
    default:
        break;
    }
}

void NetSession::clientHandle(Sim& sim, Reader& reader)
{
    const auto type = static_cast<std::uint8_t>(reader.type());
    switch (type) {
    case static_cast<std::uint8_t>(ghost::game::net::Msg::Welcome): {
        const auto welcome = reader.pod<ghost::game::net::Welcome>();
        if (reader.ok()) {
            m_welcomeId = welcome.id;
            m_welcomeSpawn = welcome.spawn;
        }
        return;
    }
    case static_cast<std::uint8_t>(net::Msg::Zone): {
        net::ZoneMsg zone;
        zone.dir = reader.text();
        zone.yaw = reader.pod<float>();
        if (!reader.ok() || m_welcomeId == kNoPlayer) {
            return;
        }
        if (sim.zone_dir() != zone.dir && !sim.switch_zone(zone.dir)) {
            leave(sim, "Could not load the host's zone");
            return;
        }
        sim.client_reset(m_welcomeId, from_glm(m_welcomeSpawn), zone.yaw, m_name);
        m_welcomed = true;
        m_status = "Joined as player " + std::to_string(static_cast<int>(m_welcomeId) + 1);
        log_info("net: %s", m_status.c_str());
        return;
    }
    case static_cast<std::uint8_t>(ghost::game::net::Msg::Refused): {
        const std::string why = reader.text();
        leave(sim, reader.ok() ? why : "The host refused the connection");
        return;
    }
    default:
        break;
    }
    if (!m_welcomed) {
        return;
    }
    if (type == static_cast<std::uint8_t>(net::Msg::World)) {
        if (auto snapshot = net::decodeWorld(reader)) {
            sim.queue_snapshot(std::move(*snapshot));
        }
    } else if (type == static_cast<std::uint8_t>(net::Msg::Screen)) {
        const TermMirror mirror = reader.pod<TermMirror>();
        if (reader.ok()) {
            sim.queue_term_mirror(mirror);
        }
    } else if (type == static_cast<std::uint8_t>(ghost::game::net::Msg::Projectile)) {
        const auto projectile = reader.pod<ghost::game::Projectile>();
        if (reader.ok()) {
            sim.queue_projectile(projectile);
        }
    } else if (type == static_cast<std::uint8_t>(ghost::game::net::Msg::Impulse)) {
        const auto push = reader.pod<glm::vec3>();
        if (reader.ok()) {
            sim.queue_impulse(push);
        }
    } else if (type == static_cast<std::uint8_t>(ghost::game::net::Msg::Give)) {
        const auto round = reader.pod<ghost::game::Round>();
        if (reader.ok()) {
            sim.queue_give(round);
        }
    } else if (type == static_cast<std::uint8_t>(ghost::game::net::Msg::Events)) {
        if (const auto events = ghost::game::net::decodeEvents(reader)) {
            for (const ghost::game::GameEvent& event : *events) {
                sim.queue_host_event(event);
            }
        }
    }
}

void NetSession::commands(const Sim& sim, const PlayerCommand& local, std::vector<SlotCommand>& out)
{
    out.clear();
    SlotCommand mine;
    mine.id = sim.local_id();
    mine.cmd = local;
    out.push_back(mine);
    if (m_role != Role::Host) {
        return;
    }
    for (Remote& r : m_remotes) {
        SlotCommand c;
        c.id = r.id;
        c.cmd = r.latest;
        if (!r.intents.empty()) {
            net::mergeEdges(c.cmd, r.intents.front());
            r.intents.pop_front();
        }
        c.has_body = r.freshBody;
        c.body = r.body;
        c.has_car = r.freshCar;
        c.car = r.car;
        c.has_gun = r.freshGun;
        c.gun = r.gun;
        r.freshBody = false;
        r.freshCar = false;
        r.freshGun = false;
        out.push_back(c);
    }
}

void NetSession::afterTick(Sim& sim, const PlayerCommand& local)
{
    ++m_ticks;
    if (!m_net) {
        return;
    }
    if (m_role == Role::Host) {
        const ghost::game::EventList& events = sim.events();
        if (m_told > events.size()) {
            m_told = 0;
        }
        if (m_told < events.size()) {
            ghost::game::EventList told;
            for (std::size_t i = m_told; i < events.size(); ++i) {
                if (!net::ownHandsOnly(events[i])) {
                    told.push_back(events[i]);
                }
            }
            if (!told.empty()) {
                m_net->broadcast(ghost::game::net::encode(told), true);
            }
        }
        m_told = events.size();
        for (const ghost::game::Projectile& p : sim.gameplay().ballistics().projectiles()) {
            if (p.id > m_sentProjectile) {
                m_net->broadcast(ghost::game::net::encode(p), true);
                m_sentProjectile = p.id;
            }
        }
        for (const auto& [id, push] : sim.take_impulses()) {
            sendTo(id, ghost::game::net::encodeImpulse(push));
        }
        for (const auto& [id, round] : sim.take_gives()) {
            sendTo(id, ghost::game::net::encodeGive(round));
        }
        if (!m_remotes.empty() && m_ticks % kSnapshotEvery == 0) {
            WorldSnapshot snapshot;
            sim.make_snapshot(snapshot);
            m_net->broadcast(net::pack(net::encodeWorld(snapshot)), false);
        }
        const bool screenOn = sim.terminal().powered();
        if (!m_remotes.empty() && (screenOn || m_screenWasOn) && m_ticks % kScreenEvery == 0) {
            TermMirror mirror;
            sim.terminal().mirror(mirror);
            std::vector<std::uint8_t> screen = net::encodeScreen(mirror);
            const bool switched = screenOn != m_screenWasOn;
            ++m_screenAge;
            if (switched || screen != m_lastScreen || m_screenAge >= kScreenRefresh) {
                m_net->broadcast(net::pack(screen), switched);
                m_lastScreen = std::move(screen);
                m_screenAge = 0;
            }
            m_screenWasOn = screenOn;
        }
    } else if (m_role == Role::Client && m_welcomed) {
        if (net::hasEdges(local)) {
            m_net->send(m_hostPeer, net::encodeIntent(local), true);
        }
        for (const ghost::game::net::ShotMsg& shot : sim.take_outgoing_shots()) {
            m_net->send(m_hostPeer, ghost::game::net::encode(shot), true);
        }
        if (auto rounds = sim.take_outgoing_rounds(); !rounds.empty()) {
            m_net->send(m_hostPeer, ghost::game::net::encodeRounds(rounds), true);
        }
        if (m_ticks % kUpdateEvery == 0) {
            const PlayerSlot* mine = sim.slot(sim.local_id());
            if (mine) {
                net::BodyMsg body;
                body.body = mine->player;
                body.command = net::continuousPart(local);
                body.gun = sim.gun_view(sim.local_id(), 1.0f);
                body.hasCar = sim.driver() == sim.local_id();
                if (body.hasCar) {
                    body.car = sim.vehicle().wire(sim.phys());
                }
                m_net->send(m_hostPeer, net::pack(net::encodeBody(body)), false);
            }
        }
    }
    m_net->flush();
}

bool NetSession::sendPhoto(std::span<const u8> rgb)
{
    if (m_role != Role::Client || !m_welcomed || !m_net) {
        return false;
    }
    m_net->send(m_hostPeer, net::pack(net::encodePhoto(rgb)), true);
    return true;
}

}
