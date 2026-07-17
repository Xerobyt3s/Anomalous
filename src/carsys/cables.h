#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct Terrain;

#define CABLE_POINTS 22
#define CABLE_LENGTH 5.2f

#define CONNECTOR_COAX_LOCAL v3(0.07f, -0.02f, -0.21f)
#define CONNECTOR_BUS_LOCAL v3(-0.07f, -0.02f, -0.21f)
#define ANTENNA_JACK_LOCAL v3(0.35f, 0.54f, 0.36f)
#define BAY_JACK_LOCAL v3(0.32f, 0.02f, -0.75f)

typedef enum CableKind {
    CABLE_COAX,
    CABLE_BUS,
    CABLE_KIND_COUNT,
} CableKind;

typedef enum CableState {
    CABLE_STOWED,
    CABLE_DRAGGED,
    CABLE_PLUGGED,
} CableState;

typedef struct Cable {
    CableState state;
    b32 linked;
    b32 sim_init;
    f32 let_out;
    Vec3 p[CABLE_POINTS];
    Vec3 prev[CABLE_POINTS];
} Cable;

void cable_reset(Cable* cable);
void cable_sim(Cable* cable, Vec3 root, const Vec3* end, const struct Terrain* terrain,
               Vec3 car_pos, Quat car_rot, f32 dt);
void cable_render(const Cable* cable);
