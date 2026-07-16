#pragma once

#include "core/types.h"

struct CarSys;
struct Vehicle;
struct PhysWorld;

void carsys_render(const struct CarSys* sys, const struct Vehicle* veh,
                   struct PhysWorld* world, f32 alpha);
