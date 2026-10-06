#include "engine/net/steam.h"

#include "engine/debug/log.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#include <steam/isteamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#include <steam/steam_api.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <deque>
#include <utility>

namespace ghost::engine::steam {
namespace {
constexpr const char* kTestAppId = "480";
constexpr int kSendRate = 1024 * 1024;
constexpr int kSendBuffer = 256 * 1024;
constexpr int kReportSeconds = 5;
constexpr const char* kGameKey = "game";
constexpr const char* kGameTag = "ghost";
constexpr const char* kVersionKey = "version";

class SteamTransport;

bool g_up = false;
SteamTransport* g_transport = nullptr;
CSteamID g_lobby;
std::string g_lobbyVersion;
std::optional<LobbyId> g_pendingJoin;
JoinState g_joinState = JoinState::None;

class SteamTransport : public Transport {
public:
    SteamTransport(HSteamListenSocket listenSocket, int maxPeers) : m_listen(listenSocket), m_maxPeers(maxPeers) {
        m_group = SteamNetworkingSockets()->CreatePollGroup();
        g_transport = this;
    }
    explicit SteamTransport(HSteamNetConnection toHost) {
        m_group = SteamNetworkingSockets()->CreatePollGroup();
        m_pending.push_back(toHost);
        g_transport = this;
    }
    ~SteamTransport() override {
        if (g_transport == this) {
            g_transport = nullptr;
        }
        if (!g_up) {
            return;
        }
        ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
        for (const auto& [id, connection] : m_peers) {
            sockets->CloseConnection(connection, 0, "left", true);
        }
        for (const HSteamNetConnection connection : m_pending) {
            sockets->CloseConnection(connection, 0, "left", false);
        }
        if (m_listen != k_HSteamListenSocket_Invalid) {
            sockets->CloseListenSocket(m_listen);
        }
        sockets->DestroyPollGroup(m_group);
    }

    bool poll(NetEvent& event) override {
        if (m_inbox.empty()) {
            SteamNetworkingMessage_t* messages[32];
            const int count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(m_group, messages, 32);
            for (int i = 0; i < count; ++i) {
                const auto* bytes = static_cast<const std::uint8_t*>(messages[i]->m_pData);
                if (const PeerId peer = peerOf(messages[i]->m_conn)) {
                    m_inbox.push_back({NetEvent::Type::Message, peer, std::vector<std::uint8_t>(bytes, bytes + messages[i]->m_cbSize)});
                }
                messages[i]->Release();
            }
        }
        if (m_inbox.empty()) {
            return false;
        }
        event = std::move(m_inbox.front());
        m_inbox.pop_front();
        return true;
    }

    void send(PeerId peer, std::span<const std::uint8_t> data, bool reliable) override {
        for (const auto& [id, connection] : m_peers) {
            if (id == peer) {
                sendTo(connection, data, reliable);
                return;
            }
        }
    }

    void broadcast(std::span<const std::uint8_t> data, bool reliable) override {
        for (const auto& [id, connection] : m_peers) {
            sendTo(connection, data, reliable);
        }
    }

    void disconnect(PeerId peer) override {
        for (const auto& [id, connection] : m_peers) {
            if (id == peer) {
                SteamNetworkingSockets()->CloseConnection(connection, 0, "dropped", true);
            }
        }
        std::erase_if(m_peers, [peer](const auto& entry) { return entry.first == peer; });
    }

    void flush() override {
        for (const auto& [id, connection] : m_peers) {
            SteamNetworkingSockets()->FlushMessagesOnConnection(connection);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastReport < std::chrono::seconds(kReportSeconds)) {
            return;
        }
        m_lastReport = now;
        for (const auto& [id, connection] : m_peers) {
            SteamNetConnectionRealTimeStatus_t status{};
            if (SteamNetworkingSockets()->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) == k_EResultOK) {
                GHOST_INFO("Steam: peer {} ping {} ms, queued {} ms, pending {} B reliable / {} B unreliable, sending {:.0f} B/s",
                           id, status.m_nPing, status.m_usecQueueTime / 1000, status.m_cbPendingReliable,
                           status.m_cbPendingUnreliable, status.m_flOutBytesPerSec);
            }
        }
    }

    void statusChanged(const SteamNetConnectionStatusChangedCallback_t& change) {
        ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
        const HSteamNetConnection connection = change.m_hConn;
        switch (change.m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:

            if (m_listen != k_HSteamListenSocket_Invalid && change.m_info.m_hListenSocket == m_listen) {
                if (static_cast<int>(m_peers.size() + m_pending.size()) < m_maxPeers && sockets->AcceptConnection(connection) == k_EResultOK) {
                    m_pending.push_back(connection);
                } else {
                    sockets->CloseConnection(connection, 0, "full", false);
                }
            }
            break;
        case k_ESteamNetworkingConnectionState_Connected: {
            if (std::erase(m_pending, connection) == 0) {
                break;
            }
            const PeerId id = m_nextPeer++;
            m_peers.emplace_back(id, connection);
            sockets->SetConnectionPollGroup(connection, m_group);
            m_inbox.push_back({NetEvent::Type::Connected, id, {}});
            break;
        }
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally: {
            const PeerId id = peerOf(connection);
            const bool pending = std::erase(m_pending, connection) > 0;
            if (id != 0 || pending) {
                GHOST_INFO("Steam: connection closed ({})", change.m_info.m_szEndDebug);
                std::erase_if(m_peers, [connection](const auto& entry) { return entry.second == connection; });
                m_inbox.push_back({NetEvent::Type::Disconnected, id, {}});
                sockets->CloseConnection(connection, 0, nullptr, false);
            }
            break;
        }
        default:
            break;
        }
    }

private:
    PeerId peerOf(HSteamNetConnection connection) const {
        for (const auto& [id, c] : m_peers) {
            if (c == connection) {
                return id;
            }
        }
        return 0;
    }
    static void sendTo(HSteamNetConnection connection, std::span<const std::uint8_t> data, bool reliable) {
        const EResult result = SteamNetworkingSockets()->SendMessageToConnection(
            connection, data.data(), static_cast<uint32>(data.size()),
            reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);

        if (result != k_EResultOK && reliable) {
            static int told = 0;
            if (told++ < 20) {
                GHOST_WARN("Steam: a reliable message of {} bytes was not sent (result {})", data.size(), static_cast<int>(result));
            }
        }
    }

    HSteamListenSocket m_listen = k_HSteamListenSocket_Invalid;
    HSteamNetPollGroup m_group = k_HSteamNetPollGroup_Invalid;
    int m_maxPeers = 1;
    std::vector<std::pair<PeerId, HSteamNetConnection>> m_peers;
    std::vector<HSteamNetConnection> m_pending;
    std::deque<NetEvent> m_inbox;
    std::chrono::steady_clock::time_point m_lastReport{};
    PeerId m_nextPeer = 1;
};

class Listener {
public:
    void createLobby(int maxMembers) {
        m_created.Set(SteamMatchmaking()->CreateLobby(k_ELobbyTypeFriendsOnly, maxMembers), this, &Listener::onCreated);
    }
    void enterLobby(CSteamID lobby) { m_entered.Set(SteamMatchmaking()->JoinLobby(lobby), this, &Listener::onEntered); }

private:
    STEAM_CALLBACK(Listener, onStatus, SteamNetConnectionStatusChangedCallback_t);
    STEAM_CALLBACK(Listener, onJoinRequested, GameLobbyJoinRequested_t);

    void onCreated(LobbyCreated_t* result, bool failed) {
        if (failed || result->m_eResult != k_EResultOK) {
            GHOST_WARN("Steam: could not create a lobby ({})", failed ? -1 : static_cast<int>(result->m_eResult));
            return;
        }
        g_lobby = CSteamID(result->m_ulSteamIDLobby);
        SteamMatchmaking()->SetLobbyData(g_lobby, kGameKey, kGameTag);
        SteamMatchmaking()->SetLobbyData(g_lobby, kVersionKey, g_lobbyVersion.c_str());
        GHOST_INFO("Steam: lobby {} is up (friends can join)", g_lobby.ConvertToUint64());
    }
    void onEntered(LobbyEnter_t* result, bool failed) {
        if (failed || result->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            GHOST_WARN("Steam: could not enter the lobby");
            g_joinState = JoinState::Failed;
            return;
        }
        g_lobby = CSteamID(result->m_ulSteamIDLobby);
        g_joinState = JoinState::Ready;
    }

    CCallResult<Listener, LobbyCreated_t> m_created;
    CCallResult<Listener, LobbyEnter_t> m_entered;
};

void Listener::onStatus(SteamNetConnectionStatusChangedCallback_t* change) {
    if (g_transport) {
        g_transport->statusChanged(*change);
    }
}

void Listener::onJoinRequested(GameLobbyJoinRequested_t* request) {
    g_pendingJoin = request->m_steamIDLobby.ConvertToUint64();
}

std::unique_ptr<Listener> g_listener;

}

bool init() {
    if (g_up) {
        return true;
    }
#ifdef _WIN32

    _putenv_s("SteamAppId", kTestAppId);
    _putenv_s("SteamGameId", kTestAppId);
#endif
    SteamErrMsg error{};
    if (SteamAPI_InitEx(&error) != k_ESteamAPIInitResult_OK) {
        GHOST_INFO("Steam is not available ({}): direct connections only", error);
        return false;
    }
    g_up = true;
    g_listener = std::make_unique<Listener>();
    SteamNetworkingUtils()->InitRelayNetworkAccess();
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, kSendRate);
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, kSendRate);
    SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, kSendBuffer);
    GHOST_INFO("Steam: signed in as {}", SteamFriends()->GetPersonaName());
    return true;
}

void shutdown() {
    if (!g_up) {
        return;
    }
    leaveLobby();
    g_listener.reset();
    g_up = false;
    SteamAPI_Shutdown();
}

void runCallbacks() {
    if (g_up) {
        SteamAPI_RunCallbacks();
    }
}

bool available() { return g_up; }

std::string personaName() { return g_up ? SteamFriends()->GetPersonaName() : std::string(); }

std::unique_ptr<Transport> listen(int maxPeers) {
    if (!g_up) {
        return nullptr;
    }
    const HSteamListenSocket socket = SteamNetworkingSockets()->CreateListenSocketP2P(0, 0, nullptr);
    if (socket == k_HSteamListenSocket_Invalid) {
        GHOST_WARN("Steam: could not listen for friends");
        return nullptr;
    }
    return std::make_unique<SteamTransport>(socket, maxPeers);
}

void createLobby(int maxMembers, std::string_view version) {
    if (g_up && g_listener) {
        g_lobbyVersion = version;
        g_listener->createLobby(maxMembers);
    }
}

LobbyId lobby() { return g_lobby.IsValid() ? g_lobby.ConvertToUint64() : 0; }

void leaveLobby() {
    if (g_up && g_lobby.IsValid()) {
        SteamMatchmaking()->LeaveLobby(g_lobby);
    }
    g_lobby = CSteamID();
    g_joinState = JoinState::None;
}

void openInviteDialog() {
    if (g_up && g_lobby.IsValid()) {
        SteamFriends()->ActivateGameOverlayInviteDialog(g_lobby);
    }
}

std::vector<FriendGame> friendsPlaying(std::string_view version) {
    std::vector<FriendGame> games;
    if (!g_up) {
        return games;
    }
    const CGameID ours(SteamUtils()->GetAppID());
    const int friends = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
    for (int i = 0; i < friends; ++i) {
        const CSteamID who = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
        FriendGameInfo_t playing;
        if (!SteamFriends()->GetFriendGamePlayed(who, &playing) || playing.m_gameID != ours || !playing.m_steamIDLobby.IsValid()) {
            continue;
        }

        const std::string game = SteamMatchmaking()->GetLobbyData(playing.m_steamIDLobby, kGameKey);
        if (game.empty()) {
            SteamMatchmaking()->RequestLobbyData(playing.m_steamIDLobby);
            continue;
        }
        if (game == kGameTag && version == SteamMatchmaking()->GetLobbyData(playing.m_steamIDLobby, kVersionKey)) {
            games.push_back({playing.m_steamIDLobby.ConvertToUint64(), SteamFriends()->GetFriendPersonaName(who)});
        }
    }
    return games;
}

std::optional<LobbyId> takePendingJoin() { return std::exchange(g_pendingJoin, std::nullopt); }

void requestJoin(LobbyId lobby) {
    if (lobby != 0) {
        g_pendingJoin = lobby;
    }
}

void joinLobby(LobbyId lobby) {
    if (!g_up || !g_listener) {
        g_joinState = JoinState::Failed;
        return;
    }
    leaveLobby();
    g_joinState = JoinState::Entering;
    g_listener->enterLobby(CSteamID(static_cast<uint64>(lobby)));
}

JoinState joinState() { return g_joinState; }

std::unique_ptr<Transport> connectToLobbyOwner() {
    if (!g_up || g_joinState != JoinState::Ready || !g_lobby.IsValid()) {
        return nullptr;
    }
    g_joinState = JoinState::None;
    SteamNetworkingIdentity host;
    host.SetSteamID(SteamMatchmaking()->GetLobbyOwner(g_lobby));
    const HSteamNetConnection connection = SteamNetworkingSockets()->ConnectP2P(host, 0, 0, nullptr);
    if (connection == k_HSteamNetConnection_Invalid) {
        GHOST_WARN("Steam: could not start a connection to the host");
        return nullptr;
    }
    return std::make_unique<SteamTransport>(connection);
}

}
