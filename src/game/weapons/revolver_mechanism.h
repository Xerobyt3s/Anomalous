#pragma once

#include "game/ammo/round.h"
#include "game/ammo/speedloader.h"
#include "game/events.h"

#include <array>
#include <cstdint>
#include <optional>

namespace ghost::game {
enum class HammerPhase : std::uint8_t { Down, Cocking, Cocked };
enum class CylinderPhase : std::uint8_t { Closed, Opening, Open, Closing };

struct MechanismTuning {
    float doubleActionPullTime = 0.32f;
    float singleActionPullTime = 0.05f;
    float cockTime = 0.24f;
    float triggerReturnTime = 0.09f;
    float doubleActionRotationEnd = 0.7f;

    float openTime = 0.38f;
    float closeTime = 0.30f;
    float ejectTime = 0.45f;
    float insertTime = 0.50f;
    float turnTime = 0.16f;
    float speedloadTime = 1.0f;
    float quickInsertTime = 0.15f;
};

struct MechanismInput {
    bool triggerHeld = false;
    bool cock = false;
    bool toggleCylinder = false;
    bool closeCylinder = false;
    bool eject = false;
    bool load = false;
    int turn = 0;
    std::optional<Round> roundToLoad;
    std::optional<SpeedloaderSlots> speedload;
    bool quick = false;
};

struct MechanismState {
    std::array<Chamber, kChamberCount> chambers{};
    int aligned = 0;
    float advance = 0.0f;
    bool rotated = false;
    float hammer = 0.0f;
    float trigger = 0.0f;
    HammerPhase phase = HammerPhase::Down;
    bool triggerNeedsReset = false;

    CylinderPhase cylinder = CylinderPhase::Closed;
    float crane = 0.0f;
    float ejectTime = -1.0f;
    int loadingChamber = -1;
    float loadProgress = 0.0f;
    bool loadingQuick = false;
    Round loadingRound;
    float turnOffset = 0.0f;
    float speedloadProgress = -1.0f;
    SpeedloaderSlots speedloadRounds{};
    bool closePending = false;

    bool isClosed() const { return cylinder == CylinderPhase::Closed; }

    float cylinderPosition() const { return static_cast<float>(aligned) + advance + turnOffset; }

    int nextToFire() const { return rotated ? aligned : (aligned + 1) % kChamberCount; }

    float ejectorStroke() const;
};

class RevolverMechanism {
public:
    void tick(const MechanismInput& input, float dt, EventList& events);

    const MechanismState& state() const { return m_state; }
    const MechanismState& previous() const { return m_previous; }
    MechanismTuning& tuning() { return m_tuning; }

    void setChamber(int index, const Chamber& chamber);
    void loadAll(const Round& round);
    void snapClosed();

    int loadTarget() const { return firstEmptyInFiringOrder(); }

private:
    void tickLockwork(const MechanismInput& input, float dt, EventList& events);
    void tickCylinder(const MechanismInput& input, float dt, EventList& events);
    void requestOpen();
    void turnBy(int chambers);
    int firstEmptyInFiringOrder() const;
    void indexForClose();
    void advanceCylinder(float progress);
    void returnTrigger(const MechanismInput& input, float dt);
    void releaseHammer(EventList& events);

    MechanismTuning m_tuning;
    MechanismState m_state;
    MechanismState m_previous;
};

}
