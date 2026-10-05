#pragma once

#include "game/weapons/revolver_mechanism.h"

#include <array>

namespace ghost::game {
struct MechanismView {
    float hammer = 0.0f;
    float trigger = 0.0f;
    float cylinder = 0.0f;
    float crane = 0.0f;
    float ejector = 0.0f;
    int loadingChamber = -1;
    ElementId loadingElement = kPlainElement;
    bool highlightSlot = false;
    float highlightPulse = 0.0f;
    float loadProgress = 0.0f;
    float speedloadProgress = -1.0f;
    int speedloadFirst = 0;
    std::array<int, kChamberCount> speedloadElements{-1, -1, -1, -1, -1, -1};
    std::array<ChamberState, kChamberCount> chambers{};
    std::array<ElementId, kChamberCount> elements{};
};

}
