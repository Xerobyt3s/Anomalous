#pragma once

#include "core/types.h"

struct Vehicle;
struct PhysWorld;

void vehicle_render(const struct Vehicle* v, struct PhysWorld* world, f32 alpha);
