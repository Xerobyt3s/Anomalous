#include "game/weapons/revolver_mechanism.h"

#include <algorithm>
#include <cmath>

namespace ghost::game {
namespace {
constexpr float kCockRotationEnd = 0.8f;

constexpr float kEjectPeak = 0.45f;

}

float MechanismState::ejectorStroke() const {
    if (ejectTime < 0.0f) {
        return 0.0f;
    }

    const float push = ejectTime / kEjectPeak;
    return ejectTime < kEjectPeak ? push * push : (1.0f - ejectTime) / (1.0f - kEjectPeak);
}

void RevolverMechanism::setChamber(int index, const Chamber& chamber) {
    m_state.chambers[static_cast<std::size_t>(index % kChamberCount)] = chamber;
}

void RevolverMechanism::loadAll(const Round& round) {
    for (Chamber& chamber : m_state.chambers) {
        chamber = {ChamberState::Live, round};
    }
}

void RevolverMechanism::advanceCylinder(float progress) {
    MechanismState& s = m_state;
    if (s.rotated) {
        return;
    }
    s.advance = std::clamp(progress, 0.0f, 1.0f);
    if (s.advance >= 1.0f) {
        s.aligned = (s.aligned + 1) % kChamberCount;
        s.advance = 0.0f;
        s.rotated = true;
    }
}

void RevolverMechanism::returnTrigger(const MechanismInput& input, float dt) {
    MechanismState& s = m_state;
    if (input.triggerHeld) {
        return;
    }
    s.trigger = std::max(0.0f, s.trigger - dt / m_tuning.triggerReturnTime);
    if (s.trigger <= 0.1f) {
        s.triggerNeedsReset = false;
    }
}

void RevolverMechanism::releaseHammer(EventList& events) {
    MechanismState& s = m_state;
    s.hammer = 0.0f;
    s.phase = HammerPhase::Down;
    s.triggerNeedsReset = true;
    s.rotated = false;
    s.advance = 0.0f;

    Chamber& chamber = s.chambers[static_cast<std::size_t>(s.aligned)];
    if (chamber.state == ChamberState::Live) {
        chamber.state = ChamberState::Spent;
        const bool last = std::none_of(s.chambers.begin(), s.chambers.end(), [](const Chamber& c) { return c.state == ChamberState::Live; });
        events.push_back(ShotFired{chamber.round, s.aligned, 0, last});
    } else {
        events.push_back(DryFired{s.aligned});
    }
}

void RevolverMechanism::requestOpen() {
    MechanismState& s = m_state;
    if (s.phase == HammerPhase::Cocking || s.trigger > 0.05f) {
        return;
    }

    s.phase = HammerPhase::Down;
    s.hammer = 0.0f;
    s.rotated = false;
    s.advance = 0.0f;
    s.closePending = false;
    s.cylinder = CylinderPhase::Opening;
}

void RevolverMechanism::turnBy(int chambers) {
    MechanismState& s = m_state;
    s.aligned = ((s.aligned + chambers) % kChamberCount + kChamberCount) % kChamberCount;
    s.turnOffset -= static_cast<float>(chambers);
}

int RevolverMechanism::firstEmptyInFiringOrder() const {
    for (int step = 0; step < kChamberCount; ++step) {
        const int index = (m_state.nextToFire() + step) % kChamberCount;
        if (m_state.chambers[static_cast<std::size_t>(index)].state == ChamberState::Empty) {
            return index;
        }
    }
    return -1;
}

void RevolverMechanism::tickLockwork(const MechanismInput& input, float dt, EventList& events) {
    MechanismState& s = m_state;
    const bool canPull = input.triggerHeld && !s.triggerNeedsReset;

    switch (s.phase) {
    case HammerPhase::Down:

        if (input.turn != 0 && !input.triggerHeld && s.trigger <= 0.05f && !input.cock) {
            s.rotated = false;
            s.advance = 0.0f;
            turnBy(input.turn);
        }
        if (input.cock && !input.triggerHeld && s.trigger <= 0.05f) {
            s.phase = HammerPhase::Cocking;
            break;
        }
        if (canPull) {
            s.trigger = std::min(1.0f, s.trigger + dt / m_tuning.doubleActionPullTime);
            s.hammer = s.trigger;
            advanceCylinder(s.trigger / m_tuning.doubleActionRotationEnd);
            if (s.trigger >= 1.0f) {
                releaseHammer(events);
            }
        } else {
            returnTrigger(input, dt);
            s.hammer = std::min(s.hammer, s.trigger);
            if (!s.rotated) {
                s.advance = std::min(s.advance, s.trigger / m_tuning.doubleActionRotationEnd);
            }
            if (s.trigger <= 0.0f) {
                s.rotated = false;
            }
        }
        break;

    case HammerPhase::Cocking:
        returnTrigger(input, dt);
        s.hammer = std::min(1.0f, s.hammer + dt / m_tuning.cockTime);
        advanceCylinder(s.hammer / kCockRotationEnd);
        if (s.hammer >= 1.0f) {
            s.phase = HammerPhase::Cocked;
            events.push_back(HammerCocked{});
        }
        break;

    case HammerPhase::Cocked:
        if (canPull) {
            s.trigger = std::min(1.0f, s.trigger + dt / m_tuning.singleActionPullTime);
            if (s.trigger >= 1.0f) {
                releaseHammer(events);
            }
        } else {
            returnTrigger(input, dt);
        }
        break;
    }
}

void RevolverMechanism::tickCylinder(const MechanismInput& input, float dt, EventList& events) {
    MechanismState& s = m_state;

    returnTrigger(MechanismInput{}, dt);

    switch (s.cylinder) {
    case CylinderPhase::Closed:
        break;

    case CylinderPhase::Opening:
        if (input.closeCylinder) {
            s.cylinder = CylinderPhase::Closing;
            break;
        }
        s.crane = std::min(1.0f, s.crane + dt / m_tuning.openTime);
        if (s.crane >= 1.0f) {
            s.cylinder = CylinderPhase::Open;
            events.push_back(CylinderOpened{});

            const bool anyLive = std::any_of(s.chambers.begin(), s.chambers.end(), [](const Chamber& c) { return c.state == ChamberState::Live; });
            const bool anySpent = std::any_of(s.chambers.begin(), s.chambers.end(), [](const Chamber& c) { return c.state == ChamberState::Spent; });
            if (!anyLive && anySpent) {
                s.ejectTime = 0.0f;
            }
        }
        break;

    case CylinderPhase::Closing:
        s.crane = std::max(0.0f, s.crane - dt / m_tuning.closeTime);
        if (s.crane <= 0.0f) {
            s.cylinder = CylinderPhase::Closed;

            s.triggerNeedsReset = true;
            events.push_back(CylinderClosed{});
        }
        break;

    case CylinderPhase::Open:

        if (input.toggleCylinder || input.closeCylinder) {
            s.closePending = true;
        }
        if (s.closePending && s.ejectTime < 0.0f && s.loadingChamber < 0 && s.speedloadProgress < 0.0f) {
            s.closePending = false;
            indexForClose();
            s.cylinder = CylinderPhase::Closing;
            break;
        }

        if (s.speedloadProgress >= 0.0f) {
            s.speedloadProgress += dt / m_tuning.speedloadTime;
            if (s.speedloadProgress >= 1.0f) {
                for (int i = 0; i < kChamberCount; ++i) {
                    if (const auto& round = s.speedloadRounds[static_cast<std::size_t>(i)]) {
                        s.chambers[static_cast<std::size_t>((s.nextToFire() + i) % kChamberCount)] = {ChamberState::Live, *round};
                    }
                }
                s.speedloadProgress = -1.0f;
                events.push_back(SpeedloaderUsed{});
            }
            break;
        }
        if (input.speedload && s.ejectTime < 0.0f && s.loadingChamber < 0) {
            const bool allEmpty = std::all_of(s.chambers.begin(), s.chambers.end(),
                                              [](const Chamber& c) { return c.state == ChamberState::Empty; });
            const bool anyRound = std::any_of(input.speedload->begin(), input.speedload->end(),
                                              [](const std::optional<Round>& r) { return r.has_value(); });
            if (allEmpty && anyRound) {
                s.speedloadRounds = *input.speedload;
                s.speedloadProgress = 0.0f;
                break;
            }
        }

        if (s.ejectTime >= 0.0f) {
            const float before = s.ejectTime;
            s.ejectTime += dt / m_tuning.ejectTime;
            if (before < kEjectPeak && s.ejectTime >= kEjectPeak) {
                events.push_back(ChambersEjected{s.chambers});
                s.chambers = {};
            }
            if (s.ejectTime >= 1.0f) {
                s.ejectTime = -1.0f;
            }
            break;
        }
        if (input.eject && s.loadingChamber < 0) {
            s.ejectTime = 0.0f;
            break;
        }

        if (s.loadingChamber >= 0) {
            s.loadProgress += dt / (s.loadingQuick ? m_tuning.quickInsertTime : m_tuning.insertTime);
            if (s.loadProgress >= 1.0f) {
                s.chambers[static_cast<std::size_t>(s.loadingChamber)] = {ChamberState::Live, s.loadingRound};
                events.push_back(RoundLoaded{s.loadingChamber, s.loadingRound});
                s.loadingChamber = -1;
                s.loadProgress = 0.0f;
                turnBy(1);
            }
            break;
        }

        if (input.turn != 0) {
            turnBy(input.turn);
        }

        if (input.load && input.roundToLoad) {
            const int target = firstEmptyInFiringOrder();
            if (target >= 0) {
                turnBy((target - s.nextToFire() + kChamberCount) % kChamberCount);
                s.loadingChamber = target;
                s.loadingRound = *input.roundToLoad;
                s.loadingQuick = input.quick;
                s.loadProgress = 0.0f;
            }
        }
        break;
    }
}

void RevolverMechanism::snapClosed() {
    MechanismState& s = m_state;
    if (s.isClosed()) {
        return;
    }
    if (s.loadingChamber >= 0) {
        s.chambers[static_cast<std::size_t>(s.loadingChamber)] = {ChamberState::Live, s.loadingRound};
        s.loadingChamber = -1;
    }
    if (s.speedloadProgress >= 0.0f) {
        for (int i = 0; i < kChamberCount; ++i) {
            if (const auto& round = s.speedloadRounds[static_cast<std::size_t>(i)]) {
                s.chambers[static_cast<std::size_t>((s.nextToFire() + i) % kChamberCount)] = {ChamberState::Live, *round};
            }
        }
        s.speedloadProgress = -1.0f;
    }
    s.ejectTime = -1.0f;
    s.loadProgress = 0.0f;
    s.closePending = false;
    indexForClose();
    s.crane = 0.0f;
    s.cylinder = CylinderPhase::Closed;
    s.triggerNeedsReset = true;
    m_previous = s;
}

void RevolverMechanism::indexForClose() {
    MechanismState& s = m_state;
    for (int step = 0; step < kChamberCount; ++step) {
        const int index = (s.nextToFire() + step) % kChamberCount;
        if (s.chambers[static_cast<std::size_t>(index)].state == ChamberState::Live) {
            turnBy(step);
            return;
        }
    }
}

void RevolverMechanism::tick(const MechanismInput& input, float dt, EventList& events) {
    m_previous = m_state;

    if (m_state.isClosed()) {
        if (input.toggleCylinder) {
            requestOpen();
        }
    }
    if (m_state.isClosed()) {
        tickLockwork(input, dt, events);
    } else {
        tickCylinder(input, dt, events);
    }

    MechanismState& s = m_state;
    const float decay = dt / m_tuning.turnTime * std::max(1.0f, std::abs(s.turnOffset));
    s.turnOffset = s.turnOffset > 0.0f ? std::max(0.0f, s.turnOffset - decay) : std::min(0.0f, s.turnOffset + decay);
}

}
