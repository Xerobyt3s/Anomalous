#pragma once

#include "core/types.h"
#include "core/pool.h"
#include "math/vmath.h"
#include "carsys/parts.h"
#include "carsys/items.h"

struct Player;
struct Vehicle;
struct CarSys;
struct World;
struct PhysWorld;

#define INTERACT_RANGE 2.6f
#define INTERACT_HOLD_TIME 1.2f

typedef enum InteractAction {
    ACTION_NONE,
    ACTION_INFO,
    ACTION_OPEN_DOOR,
    ACTION_CLOSE_DOOR,
    ACTION_ENTER_CAR,
    ACTION_EXIT_CAR,
    ACTION_HANDBRAKE,
    ACTION_INSERT_KEY,
    ACTION_CRANK,
    ACTION_ENGINE_OFF,
    ACTION_FUEL_CAP,
    ACTION_COMPUTER,
    ACTION_TERMINAL_USE,
    ACTION_REMOVE_PART,
    ACTION_INSTALL_PART,
    ACTION_TOGGLE_HOOD,
    ACTION_TOGGLE_TRUNK,
    ACTION_PLACE_CARGO,
    ACTION_TAKE_CARGO,
    ACTION_PICKUP,
    ACTION_REFUEL,
    ACTION_OIL_FILL,
    ACTION_CABLE_GRAB,
    ACTION_CABLE_PLUG,
    ACTION_CABLE_PLUG_CAMERA,
    ACTION_CABLE_PLUG_TOWER,
    ACTION_CABLE_ROUTE_REEL,
    ACTION_CABLE_UNPLUG,
    ACTION_DISK_INSERT,
    ACTION_DISK_EJECT,
} InteractAction;

typedef struct Interact {
    Item hands;
    b32 has_key;
    InteractAction action;
    PartKind target_part;
    Handle target_entity;
    i32 target_side;
    u32 target_cargo;
    Vec3 target_center;
    Vec3 target_half;
    Vec3 place_pos;
    f32 hold_time;
    f32 hold_progress;
    b32 action_is_hold;
    b32 crank_latch;
    b32 use_terminal_request;
    i32 target_cable;
    i32 cable_drag;
    b32 tower_present;
    Vec3 tower_port;
    char prompt[96];
} Interact;

void interact_init(Interact* it);
void interact_update(Interact* it, struct Player* player, struct Vehicle* veh,
                     struct CarSys* sys, struct World* world, struct PhysWorld* phys,
                     Ray view_ray, b32 e_down, b32 e_pressed, f32 dt);
b32  interact_drop(Interact* it, struct World* world, struct PhysWorld* phys,
                   Vec3 origin, Vec3 dir, f32 power);
Handle interact_spawn_pickup(struct World* world, struct PhysWorld* phys, Item item,
                             Vec3 pos, f32 yaw, Vec3 vel);
