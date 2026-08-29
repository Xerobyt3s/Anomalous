#pragma once

#include "core/types.h"
#include "math/vmath.h"

namespace anom {

class DebugDraw;
class Input;
struct Camera;

enum class GizmoMode : u32 {
    Translate,
    Rotate,
    Scale,
};

struct GizmoSnap {
    f32 pos = 0.0f;
    f32 angle_deg = 0.0f;
    f32 scale = 0.0f;
};

struct GizmoTarget {
    Vec3* pos = nullptr;
    f32* yaw = nullptr;
    f32* scale = nullptr;
};

class Gizmo {
public:
    static constexpr f32 kPickPixels = 12.0f;
    static constexpr f32 kDeadZonePixels = 14.0f;
    static constexpr f32 kHandleFraction = 0.16f;

    void init();

    bool update(const Input& input, const Camera& cam, Vec2 viewport, const GizmoTarget& target,
                const GizmoSnap& snap);
    void render(DebugDraw& debug, const Camera& cam, Vec3 pos) const;

    GizmoMode mode() const { return mode_; }
    void set_mode(GizmoMode mode) { mode_ = mode; }
    Quat basis() const { return basis_; }
    void set_basis(Quat basis) { basis_ = basis; }
    bool dragging() const { return dragging_; }
    i32 active_axis() const { return active_axis_; }

    Vec3 axis_dir(i32 axis) const;
    static f32 snap_to(f32 value, f32 step);

private:
    f32 handle_length(const Camera& cam, Vec3 pos) const;
    i32 pick_axis(const Camera& cam, Vec2 viewport, Vec3 pos, f32 handle_len, Vec2 mouse) const;
    bool pick_ring(const Camera& cam, Vec2 viewport, Vec3 pos, f32 radius, Vec2 mouse) const;

    GizmoMode mode_ = GizmoMode::Translate;
    Quat basis_ = quat_identity();
    i32 active_axis_ = -1;
    bool dragging_ = false;
    f32 drag_start_param_ = 0.0f;
    f32 drag_delta_ = 0.0f;
    Vec3 start_pos_;
    f32 start_yaw_ = 0.0f;
    f32 start_scale_ = 1.0f;
};

} // namespace anom
