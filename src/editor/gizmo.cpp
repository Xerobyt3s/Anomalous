#include "editor/gizmo.h"
#include "platform/input.h"
#include "render/camera.h"
#include "render/debug_draw.h"

namespace anom {
namespace {

u32 axis_color(i32 axis, bool hot)
{
    if (hot) {
        return kDdYellow;
    }
    if (axis == 0) {
        return kDdRed;
    }
    if (axis == 1) {
        return kDdGreen;
    }
    return kDdBlue;
}

f32 segment_distance(Vec2 p, Vec2 a, Vec2 b)
{
    const Vec2 ab = b - a;
    const Vec2 ap = p - a;
    const f32 denom = dot(ab, ab);
    const f32 t = denom > 1e-6f ? f_clamp01(dot(ap, ab) / denom) : 0.0f;
    return length(p - (a + ab * t));
}

} // namespace

void Gizmo::init()
{
    *this = Gizmo{};
}

f32 Gizmo::snap_to(f32 value, f32 step)
{
    if (step <= 0.0f) {
        return value;
    }
    return std::floor(value / step + 0.5f) * step;
}

Vec3 Gizmo::axis_dir(i32 axis) const
{
    const Vec3 unit = axis == 0 ? Vec3{1.0f, 0.0f, 0.0f}
                                : (axis == 1 ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{0.0f, 0.0f, 1.0f});
    return rotate(basis_, unit);
}

f32 Gizmo::handle_length(const Camera& cam, Vec3 pos) const
{
    return distance(cam.pos, pos) * kHandleFraction;
}

i32 Gizmo::pick_axis(const Camera& cam, Vec2 viewport, Vec3 pos, f32 handle_len, Vec2 mouse) const
{
    Vec2 origin;
    if (!cam.project_to_screen(pos, viewport, origin)) {
        return -1;
    }
    if (length(mouse - origin) < kDeadZonePixels) {
        return -1;
    }

    f32 best = kPickPixels;
    i32 picked = -1;
    for (i32 axis = 0; axis < 3; axis++) {
        Vec2 tip;
        if (!cam.project_to_screen(pos + axis_dir(axis) * handle_len, viewport, tip)) {
            continue;
        }
        const f32 d = segment_distance(mouse, origin, tip);
        if (d < best) {
            best = d;
            picked = axis;
        }
    }
    return picked;
}

bool Gizmo::pick_ring(const Camera& cam, Vec2 viewport, Vec3 pos, f32 radius, Vec2 mouse) const
{
    Vec2 prev{};
    bool have_prev = false;
    f32 best = 1e9f;

    for (i32 i = 0; i <= 32; i++) {
        const f32 angle = static_cast<f32>(i) / 32.0f * kTau;
        const Vec3 rim = rotate(basis_, Vec3{std::cos(angle) * radius, 0.0f,
                                             std::sin(angle) * radius});
        Vec2 screen;
        if (!cam.project_to_screen(pos + rim, viewport, screen)) {
            have_prev = false;
            continue;
        }
        if (have_prev) {
            best = f_min(best, segment_distance(mouse, prev, screen));
        }
        prev = screen;
        have_prev = true;
    }
    return best < kPickPixels;
}

bool Gizmo::update(const Input& input, const Camera& cam, Vec2 viewport,
                   const GizmoTarget& target, const GizmoSnap& snap)
{
    if (!target.pos) {
        return false;
    }
    const Vec2 mouse = input.mouse_pos();
    const Ray ray = cam.mouse_ray(mouse, viewport);
    const f32 handle_len = handle_length(cam, *target.pos);

    if (!dragging_) {
        if (!input.pressed(MouseButton::Left) || input.down(MouseButton::Right)) {
            return false;
        }
        i32 axis = -1;
        if (mode_ == GizmoMode::Rotate) {
            if (target.yaw && pick_ring(cam, viewport, *target.pos, handle_len, mouse)) {
                axis = 1;
            }
        } else if (mode_ != GizmoMode::Scale || target.scale) {
            axis = pick_axis(cam, viewport, *target.pos, handle_len, mouse);
        }
        if (axis < 0) {
            return false;
        }

        dragging_ = true;
        active_axis_ = axis;
        start_pos_ = *target.pos;
        start_yaw_ = target.yaw ? *target.yaw : 0.0f;
        start_scale_ = target.scale ? *target.scale : 1.0f;
        drag_delta_ = 0.0f;
        drag_start_param_ = closest_point_on_line_to_ray(*target.pos, axis_dir(axis), ray);
        return true;
    }

    if (!input.down(MouseButton::Left)) {
        dragging_ = false;
        active_axis_ = -1;
        return true;
    }

    const Vec3 dir = axis_dir(active_axis_);
    if (mode_ == GizmoMode::Translate) {
        const f32 param = closest_point_on_line_to_ray(start_pos_, dir, ray);
        Vec3 next = start_pos_ + dir * (param - drag_start_param_);
        next.x = snap_to(next.x, snap.pos);
        next.y = snap_to(next.y, snap.pos);
        next.z = snap_to(next.z, snap.pos);
        *target.pos = next;
    } else if (mode_ == GizmoMode::Rotate && target.yaw) {
        drag_delta_ += input.mouse_delta().x * 0.01f;
        f32 next = start_yaw_ + drag_delta_;
        if (snap.angle_deg > 0.0f) {
            next = snap_to(next, snap.angle_deg * kDegToRad);
        }
        *target.yaw = f_wrap_angle(next);
    } else if (mode_ == GizmoMode::Scale && target.scale) {
        drag_delta_ += input.mouse_delta().x * 0.01f;
        *target.scale = f_clamp(snap_to(start_scale_ + drag_delta_, snap.scale), 0.02f, 100.0f);
    }
    return true;
}

void Gizmo::render(DebugDraw& debug, const Camera& cam, Vec3 pos) const
{
    const f32 handle_len = handle_length(cam, pos);
    debug.overlay(true);
    if (mode_ == GizmoMode::Rotate) {
        const bool hot = dragging_ && active_axis_ == 1;
        debug.circle(pos, rotate(basis_, Vec3{0.0f, 1.0f, 0.0f}), handle_len, axis_color(1, hot));
    } else {
        for (i32 axis = 0; axis < 3; axis++) {
            const bool hot = dragging_ && active_axis_ == axis;
            debug.arrow(pos, pos + axis_dir(axis) * handle_len, handle_len * 0.12f,
                        axis_color(axis, hot));
        }
    }
    debug.overlay(false);
}

} // namespace anom
