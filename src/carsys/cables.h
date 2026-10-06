#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "physics/body.h"

namespace anom {
class PhysWorld;
class Terrain;

inline constexpr u32 kCablePoints = 64;
inline constexpr f32 kCableLength = 5.2f;
inline constexpr f32 kReelLength = 15.0f;
inline constexpr f32 kCableRadius = 0.014f;

inline constexpr Vec3 kConnectorCoaxLocal{0.07f, -0.02f, -0.21f};
inline constexpr Vec3 kConnectorBusLocal{-0.07f, -0.02f, -0.21f};
inline constexpr Vec3 kAntennaJackLocal{0.35f, 0.54f, 0.36f};
inline constexpr Vec3 kBayJackLocal{0.32f, 0.02f, -0.75f};
inline constexpr Vec3 kLoosePrinterJackLocal{0.09f, 0.05f, -0.185f};
inline constexpr Vec3 kPrinterJackLocal{0.42f, -0.17f, 0.695f};
inline constexpr Vec3 kPrinterJackHalf{0.05f, 0.05f, 0.05f};

enum CableKind : u32 {
    CABLE_COAX = 0,
    CABLE_BUS,
    CABLE_KIND_COUNT,
};

enum class CableState : u32 {
    Stowed,
    Dragged,
    Plugged,
};

struct CableObstacle {
    Vec3 pos;
    Quat rot;
    Vec3 center;
    Vec3 half;
};

struct CableSimInput {
    Vec3 root;
    const Vec3* end = nullptr;
    const Vec3* anchor = nullptr;
    const Terrain* terrain = nullptr;
    PhysWorld* phys = nullptr;
    Vec3 car_pos;
    Quat car_rot = quat_identity();
    const CableObstacle* obstacles = nullptr;
    u32 obstacle_count = 0;
};

struct Cable {
    CableState state = CableState::Stowed;
    bool linked = false;
    bool sim_init = false;
    bool via_reel = false;
    f32 let_out = 0.0f;
    Vec3 p[kCablePoints];
    Vec3 prev[kCablePoints];

    void reset();
    f32 max_len() const;
    f32 current_length() const;
    f32 span(Vec3 root, Vec3 end, const Vec3* anchor) const;
    void sim(const CableSimInput& in, f32 dt);
};

}
