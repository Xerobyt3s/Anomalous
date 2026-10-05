#include "game/net/protocol.h"

#include <variant>

namespace ghost::game::net {
namespace {
template <typename... Ts>
constexpr bool allPlain(const std::variant<Ts...>*) {
    return (std::is_trivially_copyable_v<Ts> && ...);
}
static_assert(allPlain(static_cast<const GameEvent*>(nullptr)), "every GameEvent must be trivially copyable to go over the wire");

template <typename... Ts>
constexpr std::uint32_t sizeSum(const std::variant<Ts...>*) {
    std::uint32_t sum = 0;
    std::uint32_t k = 1;
    ((sum += static_cast<std::uint32_t>(sizeof(Ts)) * (k++)), ...);
    return sum;
}

template <std::size_t I = 0>
bool readEvent(Reader& reader, std::size_t index, EventList& out) {
    if constexpr (I < std::variant_size_v<GameEvent>) {
        if (index == I) {
            out.emplace_back(reader.pod<std::variant_alternative_t<I, GameEvent>>());
            return reader.ok();
        }
        return readEvent<I + 1>(reader, index, out);
    } else {
        (void)reader;
        (void)index;
        (void)out;
        return false;
    }
}

}

std::uint32_t protocolVersion() {
    constexpr std::uint32_t kRevision = 3;
    return kRevision * 1000003u + sizeSum(static_cast<const GameEvent*>(nullptr)) * 31u +
           static_cast<std::uint32_t>(sizeof(PlayerWire) * 7 + sizeof(Ghost) * 11 + sizeof(ElementVolume) * 13 + sizeof(RosterEntry) * 17 +
                                      sizeof(Projectile) * 19 + sizeof(CrateWire) * 23 + sizeof(RoundWire) * 29 + sizeof(DropWire) * 37 +
                                      sizeof(ShotMsg) * 41 + std::variant_size_v<GameEvent> * 43);
}

std::vector<std::uint8_t> encode(const Hello& hello) {
    Writer w(Msg::Hello);
    w.pod(hello.version);
    w.text(hello.name);
    return std::move(w.bytes);
}

std::optional<Hello> decodeHello(Reader& reader) {
    Hello hello;
    hello.version = reader.pod<std::uint32_t>();
    hello.name = reader.text();
    return reader.ok() ? std::optional<Hello>(hello) : std::nullopt;
}

std::vector<std::uint8_t> encode(const Welcome& welcome) {
    Writer w(Msg::Welcome);
    w.pod(welcome);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encode(const PlayerWire& player) {
    Writer w(Msg::PlayerUpdate);
    w.pod(player);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encode(const ShotMsg& shot) {
    Writer w(Msg::Shot);
    w.pod(shot);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encode(const BenchDelta& delta) {
    Writer w(Msg::BenchDelta);
    w.pod(delta);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encode(const Snapshot& s) {
    Writer w(Msg::Snapshot);
    w.pod(s.tick);
    w.pod(s.friendlyFire);
    w.pod(s.arena);
    w.pod(s.arenaMap);
    w.list(s.players);
    w.list(s.roster);
    w.list(s.ghosts);
    w.list(s.crates);
    w.list(s.volumes);
    w.list(s.materialDrops);
    w.list(s.rounds);
    w.list(s.materials);
    return std::move(w.bytes);
}

std::optional<Snapshot> decodeSnapshot(Reader& reader) {
    Snapshot s;
    s.tick = reader.pod<std::uint32_t>();
    s.friendlyFire = reader.pod<bool>();
    s.arena = reader.pod<bool>();
    s.arenaMap = reader.pod<std::uint8_t>();
    s.players = reader.list<PlayerWire>();
    s.roster = reader.list<RosterEntry>();
    s.ghosts = reader.list<Ghost>();
    s.crates = reader.list<CrateWire>();
    s.volumes = reader.list<ElementVolume>();
    s.materialDrops = reader.list<DropWire>();
    s.rounds = reader.list<RoundWire>();
    s.materials = reader.list<std::int32_t>();
    return reader.ok() ? std::optional<Snapshot>(std::move(s)) : std::nullopt;
}

std::vector<std::uint8_t> encode(const EventList& events) {
    Writer w(Msg::Events);
    w.pod(static_cast<std::uint16_t>(events.size()));
    for (const GameEvent& event : events) {
        w.pod(static_cast<std::uint8_t>(event.index()));
        std::visit([&w](const auto& e) { w.pod(e); }, event);
    }
    return std::move(w.bytes);
}

std::optional<EventList> decodeEvents(Reader& reader) {
    EventList events;
    const std::size_t count = reader.pod<std::uint16_t>();
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t index = reader.pod<std::uint8_t>();
        if (!reader.ok() || !readEvent(reader, index, events)) {
            return std::nullopt;
        }
    }
    return events;
}

std::vector<std::uint8_t> encode(const Projectile& projectile) {
    Writer w(Msg::Projectile);
    w.pod(projectile);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeRounds(const std::vector<RoundWire>& rounds) {
    Writer w(Msg::DropRounds);
    w.list(rounds);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeImpulse(const glm::vec3& deltaVelocity) {
    Writer w(Msg::Impulse);
    w.pod(deltaVelocity);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodePlace(const glm::vec3& position) {
    Writer w(Msg::Place);
    w.pod(position);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeSpend(std::uint8_t chamber) {
    Writer w(Msg::Spend);
    w.pod(chamber);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeRespawn(const glm::vec3& position, bool arena, std::uint8_t arenaMap) {
    Writer w(Msg::Respawn);
    w.pod(arena);
    w.pod(arenaMap);
    w.pod(position);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeGive(const Round& round) {
    Writer w(Msg::Give);
    w.pod(round);
    return std::move(w.bytes);
}

std::vector<std::uint8_t> encodeRefused(const std::string& why) {
    Writer w(Msg::Refused);
    w.text(why);
    return std::move(w.bytes);
}

}
