#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace ghost::game {
struct TravelArmed {
    int destination = -1;
};

struct TravelStarted {
    int destination = -1;
};

struct TravelArrived {
    int destination = -1;
};

struct TowerBreached {};

struct TapeWritten {};

struct PortLinked {
    int port = 0;
};

struct CableDropped {
    glm::vec3 position{0.0f};
};

struct TerminalClicked {};

struct TerminalPowered {
    bool on = false;
};

struct PhotoTaken {
    bool saved = false;
    unsigned left = 0;
    std::uint8_t player = 0;
};

struct DoorMoved {
    int side = 0;
    bool opening = false;
    glm::vec3 position{0.0f};
};

struct HoodMoved {
    bool opening = false;
    glm::vec3 position{0.0f};
};

struct TrunkMoved {
    bool opening = false;
    glm::vec3 position{0.0f};
};

struct EngineStarted {
    glm::vec3 position{0.0f};
};

struct CarImpact {
    float strength = 0.0f;
    glm::vec3 position{0.0f};
};

struct PartInstalled {
    int part = 0;
    glm::vec3 position{0.0f};
};

struct PartRemoved {
    int part = 0;
    glm::vec3 position{0.0f};
};

struct HandbrakeMoved {
    bool set = false;
    glm::vec3 position{0.0f};
};

struct HeadlightsSwitched {
    bool on = false;
};

struct WipersSwitched {
    int mode = 0;
};

struct FuelCapMoved {
    bool open = false;
    glm::vec3 position{0.0f};
};

struct Refuelled {
    float amount = 0.0f;
    glm::vec3 position{0.0f};
};

struct OilFilled {
    glm::vec3 position{0.0f};
};

struct KeyMoved {
    bool inserted = false;
};

struct CargoMoved {
    bool placed = false;
    glm::vec3 position{0.0f};
};

struct DiskMoved {
    bool inserted = false;
};

struct TapeMoved {
    bool inserted = false;
};

struct DeckPlay {
    bool on = false;
};

struct GearShifted {
    int gear = 0;
    bool manual = false;
};

struct EngineStopped {
    bool stalled = false;
    glm::vec3 position{0.0f};
};

struct TankFilled {
    int count = 0;
};

struct ZoneLoaded {
    int scene = -1;
};

struct PlayerPlaced {
    std::uint8_t player = 0;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
};

struct SeatRefused {
    std::uint8_t player = 0;
};

}
