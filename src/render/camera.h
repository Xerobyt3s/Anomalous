#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class Input;

struct Camera {
    Vec3 pos{0.0f, 0.0f, 0.0f};
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;
    Quat frame = quat_identity();
    f32 roll = 0.0f;
    f32 fov_y = 70.0f * kDegToRad;
    f32 znear = 0.05f;
    f32 zfar = 2000.0f;
    f32 move_speed = 10.0f;

    void look_at(Vec3 target);

    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    Mat4 view() const;
    Mat4 proj(f32 aspect) const;

    Ray mouse_ray(Vec2 mouse, Vec2 viewport) const;
    bool project_to_screen(Vec3 world, Vec2 viewport, Vec2& out_screen) const;

    bool fly_update(const Input& input, f32 dt);
};

} // namespace anom
