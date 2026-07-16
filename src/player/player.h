#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct PhysWorld;
struct Vehicle;
struct Camera;

typedef enum PlayerState {
    PLAYER_ON_FOOT,
    PLAYER_ENTERING,
    PLAYER_DRIVING,
    PLAYER_EXITING,
} PlayerState;

typedef struct PlayerCommand {
    f32 move_x;
    f32 move_z;
    b32 run;
    b32 jump;
    b32 interact;
} PlayerCommand;

typedef struct Player {
    PlayerState state;
    Vec3 pos;
    Vec3 prev_pos;
    Vec3 vel;
    f32 yaw;
    f32 pitch;
    b32 grounded;
    f32 transition_t;
    Vec3 transition_eye;
    f32 transition_yaw;
    f32 transition_pitch;
    Vec3 exit_pos;
    Vec3 cockpit_eye;
    b32 cockpit_eye_valid;
    f32 look_yaw;
    f32 look_pitch;
} Player;

void player_init(Player* p, Vec3 pos, f32 yaw);
void player_tick(Player* p, struct PhysWorld* phys, struct Vehicle* veh, PlayerCommand cmd, f32 dt);
void player_look(Player* p, f32 dx, f32 dy);
b32  player_driving(const Player* p);
b32  player_can_enter(const Player* p, struct PhysWorld* phys, const struct Vehicle* veh);
b32  player_can_exit(const Player* p, struct PhysWorld* phys, const struct Vehicle* veh);
void player_camera(Player* p, struct PhysWorld* phys, const struct Vehicle* veh, f32 alpha, f32 dt, struct Camera* cam);
void player_debug_draw(const Player* p, f32 alpha);
