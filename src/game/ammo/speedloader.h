#pragma once

#include "game/ammo/round.h"

#include <array>
#include <optional>

namespace ghost::game {
constexpr int kSpeedloaderSlots = 6;
constexpr int kSpeedloadersCarried = 2;

using SpeedloaderSlots = std::array<std::optional<Round>, kSpeedloaderSlots>;

struct Speedloader {
    SpeedloaderSlots slots{};

    int count() const {
        int n = 0;
        for (const auto& slot : slots) {
            n += slot ? 1 : 0;
        }
        return n;
    }
    bool empty() const { return count() == 0; }
};

struct SpeedloaderRack {
    std::array<Speedloader, kSpeedloadersCarried> loaders{};

    int nextFilled() const {
        for (int i = 0; i < kSpeedloadersCarried; ++i) {
            if (!loaders[static_cast<std::size_t>(i)].empty()) {
                return i;
            }
        }
        return -1;
    }
    int filled() const {
        int n = 0;
        for (const Speedloader& loader : loaders) {
            n += loader.empty() ? 0 : 1;
        }
        return n;
    }

    void useNext() {
        if (const int i = nextFilled(); i >= 0) {
            loaders[static_cast<std::size_t>(i)] = {};
        }
    }
};

}
