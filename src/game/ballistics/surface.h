#pragma once

#include <cstdint>

namespace ghost::game {
enum class Surface : std::uint8_t { Ground, Concrete, Wood, Steel, Ghost, Player };

struct SurfaceResponse {
    float ricochetMaxAngleDeg;
    float ricochetSpeedRetain;
};

inline SurfaceResponse surfaceResponse(Surface surface) {
    switch (surface) {
    case Surface::Ghost:
    case Surface::Player:
        return {0.0f, 0.0f};
    case Surface::Steel:
        return {25.0f, 0.7f};
    case Surface::Concrete:
        return {12.0f, 0.45f};
    case Surface::Wood:
        return {5.0f, 0.3f};
    case Surface::Ground:
    default:
        return {8.0f, 0.4f};
    }
}

inline const char* surfaceName(Surface surface) {
    switch (surface) {
    case Surface::Ghost:
    case Surface::Player:
        return "ghost";
    case Surface::Steel:
        return "steel";
    case Surface::Concrete:
        return "concrete";
    case Surface::Wood:
        return "wood";
    case Surface::Ground:
    default:
        return "ground";
    }
}

}
