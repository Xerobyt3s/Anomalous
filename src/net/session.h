#pragma once

#include "core/types.h"
#include "engine/net/transport.h"
#include "game/net/protocol.h"
#include "sim/player_slot.h"
#include "sim/wire.h"
#include "terminal/terminal.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace anom {
class Sim;

namespace net {
enum class Msg : std::uint8_t {
    Body = 100,
    Intent,
    World,
    Screen,
    Photo,
    Zone,
};

struct BodyMsg {
    Player body;
    PlayerCommand command;
    bool hasCar = false;
    CarWire car;
};

struct ZoneMsg {
    std::string dir;
    float yaw = 0.0f;
};

std::uint32_t protocolVersion();
bool hasEdges(const PlayerCommand& command);
PlayerCommand continuousPart(const PlayerCommand& command);
void mergeEdges(PlayerCommand& into, const PlayerCommand& from);

std::vector<std::uint8_t> encodeBody(const BodyMsg& body);
std::vector<std::uint8_t> encodeIntent(const PlayerCommand& command);
std::vector<std::uint8_t> encodeWorld(const WorldSnapshot& snapshot);
std::vector<std::uint8_t> encodeScreen(const TermMirror& mirror);
std::vector<std::uint8_t> encodePhoto(std::span<const u8> rgb);
std::vector<std::uint8_t> encodeZone(const ZoneMsg& zone);
std::optional<WorldSnapshot> decodeWorld(ghost::game::net::Reader& reader);

}

class NetSession {
public:
    enum class Role { Solo, Host, Client };

    static constexpr std::uint32_t kSnapshotEvery = 4;
    static constexpr std::uint32_t kUpdateEvery = 2;
    static constexpr std::uint32_t kScreenEvery = 12;

    bool host(Sim& sim, std::uint16_t port);
    bool join(Sim& sim, const std::string& address, std::uint16_t port, const std::string& name);
    void attach(std::unique_ptr<ghost::engine::Transport> transport, Role role, Sim& sim, const std::string& name);
    void leave(Sim& sim, const std::string& why);

    void receive(Sim& sim);
    void commands(const Sim& sim, const PlayerCommand& local, std::vector<SlotCommand>& out);
    void afterTick(Sim& sim, const PlayerCommand& local);
    void eventsConsumed() { m_told = 0; }
    bool sendPhoto(std::span<const u8> rgb);

    Role role() const { return m_role; }
    bool welcomed() const { return m_welcomed; }
    const std::string& status() const { return m_status; }
    const std::string& name() const { return m_name; }
    void setName(const std::string& name) { m_name = name.substr(0, kWireNameChars - 1); }

private:
    struct Remote {
        ghost::engine::PeerId peer = 0;
        PlayerId id = kNoPlayer;
        PlayerCommand latest;
        std::deque<PlayerCommand> intents;
        bool freshBody = false;
        Player body;
        bool freshCar = false;
        CarWire car;
    };

    Remote* remoteByPeer(ghost::engine::PeerId peer);
    void hostHandle(Sim& sim, ghost::engine::PeerId peer, ghost::game::net::Reader& reader);
    void clientHandle(Sim& sim, ghost::game::net::Reader& reader);

    std::unique_ptr<ghost::engine::Transport> m_net;
    Role m_role = Role::Solo;
    std::string m_name = "Player";
    std::string m_status;
    bool m_welcomed = false;
    ghost::engine::PeerId m_hostPeer = 0;
    PlayerId m_welcomeId = kNoPlayer;
    glm::vec3 m_welcomeSpawn{0.0f};
    std::vector<Remote> m_remotes;
    std::uint32_t m_ticks = 0;
    std::size_t m_told = 0;
    bool m_screenWasOn = false;
};

}
