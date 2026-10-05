#include "engine/net/steam.h"

namespace ghost::engine::steam {
bool init() { return false; }
void shutdown() {}
void runCallbacks() {}
bool available() { return false; }
std::string personaName() { return {}; }
std::unique_ptr<Transport> listen(int) { return nullptr; }
void createLobby(int, std::string_view) {}
LobbyId lobby() { return 0; }
void leaveLobby() {}
void openInviteDialog() {}
std::vector<FriendGame> friendsPlaying(std::string_view) { return {}; }
std::optional<LobbyId> takePendingJoin() { return std::nullopt; }
void requestJoin(LobbyId) {}
void joinLobby(LobbyId) {}
JoinState joinState() { return JoinState::None; }
std::unique_ptr<Transport> connectToLobbyOwner() { return nullptr; }

}
