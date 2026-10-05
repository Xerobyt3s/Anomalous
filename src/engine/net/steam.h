#pragma once

#include "engine/net/transport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ghost::engine::steam {
using LobbyId = std::uint64_t;

bool init();
void shutdown();
void runCallbacks();
bool available();
std::string personaName();

std::unique_ptr<Transport> listen(int maxPeers);

void createLobby(int maxMembers, std::string_view version);
LobbyId lobby();
void leaveLobby();
void openInviteDialog();

struct FriendGame {
    LobbyId lobby = 0;
    std::string name;
};

std::vector<FriendGame> friendsPlaying(std::string_view version);

std::optional<LobbyId> takePendingJoin();
void requestJoin(LobbyId lobby);

enum class JoinState { None, Entering, Ready, Failed };
void joinLobby(LobbyId lobby);
JoinState joinState();

std::unique_ptr<Transport> connectToLobbyOwner();

}
