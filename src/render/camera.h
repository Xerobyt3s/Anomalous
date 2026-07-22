#pragma once

#include "core/types.h"
#include "math/vmath.h"

struct GameInput;

typedef struct Camera {
    Vec3 pos;
    f32 yaw;
    f32 pitch;
    f32 fov_y;
    f32 znear;
    f32 zfar;
    f32 move_speed;
} Camera;

void camera_init(Camera* cam, Vec3 pos);
void camera_look_at(Camera* cam, Vec3 target);
Vec3 camera_forward(const Camera* cam);
Vec3 camera_right(const Camera* cam);
Mat4 camera_view(const Camera* cam);
Mat4 camera_proj(const Camera* cam, f32 aspect);
Ray  camera_mouse_ray(const Camera* cam, f32 mouse_x, f32 mouse_y, Vec2 viewport);
void camera_fly_update(Camera* cam, const struct GameInput* input, f32 dt);
