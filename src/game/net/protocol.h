#pragma once

#include "game/ballistics/ballistics.h"
#include "game/events.h"
#include "game/ghosts/ghost_world.h"
#include "game/player/player.h"
#include "game/player/roster.h"
#include "game/weapons/mechanism_view.h"
#include "game/world/element_volume.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace ghost::game::net {
inline constexpr std::uint16_t kDefaultPort = 47777;

enum class Msg : std::uint8_t {
    Hello = 1,
    PlayerUpdate,
    Shot,
    DropRounds,
    BenchDelta,

    Welcome,
    Refused,
    Snapshot,
    Events,
    Projectile,
    Impulse,
    Respawn,
    Give,
    Place,
    Spend,
};

class Writer {
public:
    explicit Writer(Msg type) { bytes.push_back(static_cast<std::uint8_t>(type)); }

    template <typename T>
    void pod(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    template <typename T>
    void list(const std::vector<T>& values) {
        static_assert(std::is_trivially_copyable_v<T>);
        pod(static_cast<std::uint16_t>(values.size()));
        const auto* p = reinterpret_cast<const std::uint8_t*>(values.data());
        bytes.insert(bytes.end(), p, p + sizeof(T) * values.size());
    }
    void text(const std::string& value) {
        pod(static_cast<std::uint16_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    std::vector<std::uint8_t> bytes;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : m_data(data) {}

    bool ok() const { return m_ok; }
    Msg type() { return static_cast<Msg>(pod<std::uint8_t>()); }

    template <typename T>
    T pod() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        if (m_at + sizeof(T) > m_data.size()) {
            m_ok = false;
            return value;
        }
        std::memcpy(static_cast<void*>(&value), m_data.data() + m_at, sizeof(T));
        m_at += sizeof(T);
        return value;
    }
    template <typename T>
    std::vector<T> list() {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::size_t count = pod<std::uint16_t>();
        std::vector<T> values;
        if (!m_ok || m_at + count * sizeof(T) > m_data.size()) {
            m_ok = false;
            return values;
        }
        values.resize(count);
        if (count > 0) {
            std::memcpy(static_cast<void*>(values.data()), m_data.data() + m_at, count * sizeof(T));
        }
        m_at += count * sizeof(T);
        return values;
    }
    std::string text() {
        const std::size_t count = pod<std::uint16_t>();
        if (!m_ok || m_at + count > m_data.size()) {
            m_ok = false;
            return {};
        }
        std::string value(reinterpret_cast<const char*>(m_data.data() + m_at), count);
        m_at += count;
        return value;
    }

private:
    std::span<const std::uint8_t> m_data;
    std::size_t m_at = 0;
    bool m_ok = true;
};

struct PlayerWire {
    PlayerId id = 0;
    char name[24] = {};
    PlayerState state;
    glm::vec3 muzzle{0.0f};
    glm::vec3 barrelDirection{0.0f, 0.0f, -1.0f};
    bool interact = false;
    bool pickup = false;
    MechanismView gun;
};

struct CrateWire {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
};

struct RoundWire {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    Round round;
    bool resting = false;
};

struct DropWire {
    std::uint16_t material = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    bool resting = false;
    float age = 0.0f;
};

struct Hello {
    std::uint32_t version = 0;
    std::string name;
};

struct Welcome {
    PlayerId id = 0;
    glm::vec3 spawn{0.0f};
    bool arena = false;
    std::uint8_t arenaMap = 0;
};

struct ShotMsg {
    glm::vec3 origin{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    float spreadDeg = 0.0f;
    Round round;
};

struct BenchDelta {
    std::uint16_t material = 0;
    std::int32_t change = 0;
};

struct Snapshot {
    std::uint32_t tick = 0;
    bool friendlyFire = true;
    bool arena = false;
    std::uint8_t arenaMap = 0;
    std::vector<PlayerWire> players;
    std::vector<RosterEntry> roster;
    std::vector<Ghost> ghosts;
    std::vector<CrateWire> crates;
    std::vector<ElementVolume> volumes;
    std::vector<DropWire> materialDrops;
    std::vector<RoundWire> rounds;
    std::vector<std::int32_t> materials;
};

std::uint32_t protocolVersion();

std::vector<std::uint8_t> encode(const Hello& hello);
std::vector<std::uint8_t> encode(const Welcome& welcome);
std::vector<std::uint8_t> encode(const PlayerWire& player);
std::vector<std::uint8_t> encode(const ShotMsg& shot);
std::vector<std::uint8_t> encode(const BenchDelta& delta);
std::vector<std::uint8_t> encode(const Snapshot& snapshot);
std::vector<std::uint8_t> encode(const EventList& events);
std::vector<std::uint8_t> encode(const Projectile& projectile);
std::vector<std::uint8_t> encodeRounds(const std::vector<RoundWire>& rounds);
std::vector<std::uint8_t> encodeImpulse(const glm::vec3& deltaVelocity);
std::vector<std::uint8_t> encodeRespawn(const glm::vec3& position, bool arena, std::uint8_t arenaMap = 0);
std::vector<std::uint8_t> encodeGive(const Round& round);
std::vector<std::uint8_t> encodePlace(const glm::vec3& position);
std::vector<std::uint8_t> encodeSpend(std::uint8_t chamber);
std::vector<std::uint8_t> encodeRefused(const std::string& why);

std::optional<Hello> decodeHello(Reader& reader);
std::optional<Snapshot> decodeSnapshot(Reader& reader);
std::optional<EventList> decodeEvents(Reader& reader);

}
