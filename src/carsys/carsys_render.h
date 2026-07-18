#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct CarSys;
struct Vehicle;
struct PhysWorld;

void carsys_render(const struct CarSys* sys, const struct Vehicle* veh,
                   struct PhysWorld* world, f32 alpha, f32 dt, u32 screen_texture);
void carsys_render_glass(const struct CarSys* sys, const struct Vehicle* veh,
                         struct PhysWorld* world, f32 alpha);
void carsys_spawn_sparks(Vec3 pos, u32 count);
