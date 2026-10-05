#include "engine/net/transport.h"

#include "engine/debug/log.h"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <enet/enet.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>

namespace ghost::engine {
namespace {
constexpr int kChannels = 2;
constexpr std::uint32_t kTimeoutMs = 5000;

bool ensureEnet() {
    static const bool ok = [] {
        if (enet_initialize() != 0) {
            GHOST_ERROR("Net: ENet failed to initialize");
            return false;
        }
        std::atexit(enet_deinitialize);
        return true;
    }();
    return ok;
}

class EnetTransport final : public Transport {
public:
    explicit EnetTransport(ENetHost* host) : m_host(host) {}
    ~EnetTransport() override {
        for (const auto& [id, peer] : m_peers) {
            enet_peer_disconnect_now(peer, 0);
        }
        enet_host_destroy(m_host);
    }

    void addPending(ENetPeer* peer) { peer->data = nullptr; }

    bool poll(NetEvent& out) override {
        ENetEvent event;
        while (enet_host_service(m_host, &event, 0) > 0) {
            switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT: {
                const PeerId id = m_nextPeer++;
                m_peers.emplace_back(id, event.peer);
                event.peer->data = reinterpret_cast<void*>(static_cast<std::uintptr_t>(id));
                enet_peer_timeout(event.peer, 0, kTimeoutMs, kTimeoutMs);
                out = {NetEvent::Type::Connected, id, {}};
                return true;
            }
            case ENET_EVENT_TYPE_DISCONNECT: {
                const auto id = static_cast<PeerId>(reinterpret_cast<std::uintptr_t>(event.peer->data));
                std::erase_if(m_peers, [&](const auto& entry) { return entry.second == event.peer; });
                out = {NetEvent::Type::Disconnected, id, {}};
                return true;
            }
            case ENET_EVENT_TYPE_RECEIVE: {
                const auto id = static_cast<PeerId>(reinterpret_cast<std::uintptr_t>(event.peer->data));
                out = {NetEvent::Type::Message, id, std::vector<std::uint8_t>(event.packet->data, event.packet->data + event.packet->dataLength)};
                enet_packet_destroy(event.packet);
                return true;
            }
            default:
                break;
            }
        }
        return false;
    }

    void send(PeerId peer, std::span<const std::uint8_t> data, bool reliable) override {
        for (const auto& [id, enetPeer] : m_peers) {
            if (id == peer) {
                enet_peer_send(enetPeer, reliable ? 0 : 1, packet(data, reliable));
                return;
            }
        }
    }

    void broadcast(std::span<const std::uint8_t> data, bool reliable) override {
        if (!m_peers.empty()) {
            enet_host_broadcast(m_host, reliable ? 0 : 1, packet(data, reliable));
        }
    }

    void disconnect(PeerId peer) override {
        for (const auto& [id, enetPeer] : m_peers) {
            if (id == peer) {
                enet_peer_disconnect(enetPeer, 0);
            }
        }
    }

    void flush() override { enet_host_flush(m_host); }

private:
    static ENetPacket* packet(std::span<const std::uint8_t> data, bool reliable) {
        return enet_packet_create(data.data(), data.size(), reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
    }

    ENetHost* m_host;
    std::vector<std::pair<PeerId, ENetPeer*>> m_peers;
    PeerId m_nextPeer = 1;
};

}

std::unique_ptr<Transport> listen(std::uint16_t port, int maxPeers) {
    if (!ensureEnet()) {
        return nullptr;
    }
    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;
    ENetHost* host = enet_host_create(&address, static_cast<std::size_t>(maxPeers), kChannels, 0, 0);
    if (!host) {
        GHOST_WARN("Net: could not listen on UDP port {} (in use?)", port);
        return nullptr;
    }
    return std::make_unique<EnetTransport>(host);
}

std::unique_ptr<Transport> connect(const std::string& address, std::uint16_t port) {
    if (!ensureEnet()) {
        return nullptr;
    }
    ENetAddress target;
    if (enet_address_set_host(&target, address.c_str()) != 0) {
        GHOST_WARN("Net: can't resolve '{}'", address);
        return nullptr;
    }
    target.port = port;
    ENetHost* host = enet_host_create(nullptr, 1, kChannels, 0, 0);
    if (!host) {
        return nullptr;
    }
    ENetPeer* peer = enet_host_connect(host, &target, kChannels, 0);
    if (!peer) {
        enet_host_destroy(host);
        return nullptr;
    }
    auto transport = std::make_unique<EnetTransport>(host);
    transport->addPending(peer);
    return transport;
}

bool LoopbackTransport::poll(NetEvent& event) {
    if (m_inbox.empty()) {
        return false;
    }
    event = std::move(m_inbox.front());
    m_inbox.pop_front();
    return true;
}

void LoopbackTransport::send(PeerId peer, std::span<const std::uint8_t> data, bool reliable) {
    if (!reliable && dropUnreliable) {
        return;
    }
    for (const auto& [id, link] : m_links) {
        if (id == peer) {
            link.other->m_inbox.push_back({NetEvent::Type::Message, link.idThere, std::vector<std::uint8_t>(data.begin(), data.end())});
        }
    }
}

void LoopbackTransport::broadcast(std::span<const std::uint8_t> data, bool reliable) {
    for (const auto& [id, link] : m_links) {
        send(id, data, reliable);
    }
}

void LoopbackTransport::disconnect(PeerId peer) {
    for (const auto& [id, link] : m_links) {
        if (id != peer) {
            continue;
        }
        LoopbackTransport* other = link.other;
        const PeerId there = link.idThere;
        other->m_inbox.push_back({NetEvent::Type::Disconnected, there, {}});
        std::erase_if(other->m_links, [&](const auto& entry) { return entry.first == there; });
        m_inbox.push_back({NetEvent::Type::Disconnected, peer, {}});
        break;
    }
    std::erase_if(m_links, [&](const auto& entry) { return entry.first == peer; });
}

std::unique_ptr<LoopbackTransport> loopbackHost() { return std::make_unique<LoopbackTransport>(); }

std::unique_ptr<LoopbackTransport> loopbackClient(LoopbackTransport& host) {
    auto client = std::make_unique<LoopbackTransport>();
    const PeerId atHost = host.m_nextPeer++;
    const PeerId atClient = client->m_nextPeer++;
    host.m_links.push_back({atHost, {client.get(), atClient}});
    client->m_links.push_back({atClient, {&host, atHost}});
    host.m_inbox.push_back({NetEvent::Type::Connected, atHost, {}});
    client->m_inbox.push_back({NetEvent::Type::Connected, atClient, {}});
    return client;
}

}
