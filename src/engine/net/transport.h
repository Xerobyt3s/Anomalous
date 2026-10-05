#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ghost::engine {
using PeerId = std::uint32_t;

struct NetEvent {
    enum class Type { Connected, Disconnected, Message };
    Type type = Type::Message;
    PeerId peer = 0;
    std::vector<std::uint8_t> data;
};

class Transport {
public:
    virtual ~Transport() = default;

    virtual bool poll(NetEvent& event) = 0;
    virtual void send(PeerId peer, std::span<const std::uint8_t> data, bool reliable) = 0;
    virtual void broadcast(std::span<const std::uint8_t> data, bool reliable) = 0;
    virtual void disconnect(PeerId peer) = 0;

    virtual void flush() {}
};

std::unique_ptr<Transport> listen(std::uint16_t port, int maxPeers);
std::unique_ptr<Transport> connect(const std::string& address, std::uint16_t port);

class LoopbackTransport : public Transport {
public:
    bool poll(NetEvent& event) override;
    void send(PeerId peer, std::span<const std::uint8_t> data, bool reliable) override;
    void broadcast(std::span<const std::uint8_t> data, bool reliable) override;
    void disconnect(PeerId peer) override;

    bool dropUnreliable = false;

private:
    friend std::unique_ptr<LoopbackTransport> loopbackClient(LoopbackTransport& host);
    struct Link {
        LoopbackTransport* other = nullptr;
        PeerId idThere = 0;
    };
    std::vector<std::pair<PeerId, Link>> m_links;
    std::deque<NetEvent> m_inbox;
    PeerId m_nextPeer = 1;
};

std::unique_ptr<LoopbackTransport> loopbackHost();

std::unique_ptr<LoopbackTransport> loopbackClient(LoopbackTransport& host);

}
