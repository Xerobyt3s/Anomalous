#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "carsys/parts.h"
#include "carsys/electrics.h"
#include "carsys/fluids.h"
#include "carsys/items.h"
#include "carsys/cables.h"

struct Vehicle;
struct PhysWorld;

#define CARSYS_CRANK_TIME 0.7f
#define CARGO_MAX 8
#define TRUNK_MIN_X -0.50f
#define TRUNK_MAX_X 0.50f
#define TRUNK_FLOOR_Y -0.20f
#define TRUNK_TOP_Y 0.16f
#define TRUNK_MIN_Z 1.45f
#define TRUNK_MAX_Z 2.03f

typedef struct CargoItem {
    Item item;
    Vec3 pos;
    Vec3 vel;
    b32 used;
    b32 supported;
} CargoItem;

typedef struct CarSys {
    PartSlot parts[PART_COUNT];
    Electrics elec;
    Fluids fluids;
    b32 engine_on;
    f32 crank_timer;
    b32 headlight_switch;
    f32 hood_open;
    b32 hood_target;
    f32 door_open[2];
    b32 door_target[2];
    f32 trunk_open;
    b32 trunk_target;
    b32 handbrake_latched;
    f32 lever_anim;
    b32 key_inserted;
    b32 crank_request;
    b32 crank_active;
    f32 crank_hold;
    b32 fuel_cap_open;
    f32 cap_anim;
    f32 popup_anim;
    b32 computer_on;
    Cable cables[CABLE_KIND_COUNT];
    CargoItem cargo[CARGO_MAX];
    Vec3 prev_vel;
    f32 impact_cooldown;
    f32 last_impact_severity;
} CarSys;

void carsys_init(CarSys* sys);
void carsys_tick(CarSys* sys, struct Vehicle* veh, struct PhysWorld* world, f32 dt);
b32  carsys_try_start(CarSys* sys, struct Vehicle* veh);
void carsys_stop_engine(CarSys* sys);
b32  carsys_cargo_add(CarSys* sys, Item item, Vec3 pos);
b32  carsys_cargo_take(CarSys* sys, u32 index, Item* out_item);
u32  carsys_cargo_count(const CarSys* sys);
f32  carsys_cargo_place_y(const CarSys* sys, ItemKind kind, f32 x, f32 z, b32* out_ok);
