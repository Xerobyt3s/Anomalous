#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class CarSys;
class PhysWorld;
class RenderDevice;
class Vehicle;

inline constexpr u32 kMaxPuffs = 96;

class CarSysRenderer {
public:
    void draw(RenderDevice& device, const CarSys& sys, const Vehicle& veh, PhysWorld& phys,
              f32 alpha, f32 dt, u32 screen_texture);
    void draw_glass(RenderDevice& device, const CarSys& sys, const Vehicle& veh, PhysWorld& phys,
                    f32 alpha, f32 time);
    void spawn_sparks(Vec3 pos, u32 count);

private:
    struct Puff {
        Vec3 pos;
        Vec3 vel;
        f32 life = 0.0f;
        f32 max_life = 0.0f;
        f32 size = 0.0f;
        bool spark = false;
        bool used = false;
    };

    f32 rand01();
    void spawn_puff(Vec3 pos, Vec3 vel, f32 life, f32 size, bool spark);
    void draw_effects(RenderDevice& device, const CarSys& sys, const Vehicle& veh,
                      const Mat4& base, Vec3 gravity, f32 dt);

    Puff puffs_[kMaxPuffs];
    u32 puff_next_ = 0;
    f32 smoke_accum_ = 0.0f;
    u32 rng_ = 0x9E3779B9u;
};

} // namespace anom
