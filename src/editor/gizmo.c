#include "editor/gizmo.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/debug_draw.h"

#define GIZMO_PICK_PIXELS 12.0f
#define GIZMO_DEAD_ZONE_PIXELS 14.0f
#define GIZMO_HANDLE_FRACTION 0.16f

static Vec3 gizmo_axis_dir(const Gizmo* gizmo, i32 axis)
{
    Vec3 dir = axis == 0 ? v3(1.0f, 0.0f, 0.0f)
               : (axis == 1 ? v3(0.0f, 1.0f, 0.0f) : v3(0.0f, 0.0f, 1.0f));
    return quat_rotate_vec3(gizmo->basis, dir);
}

static u32 gizmo_axis_color(i32 axis, b32 hot)
{
    if (hot) {
        return DD_YELLOW;
    }
    if (axis == 0) {
        return DD_RED;
    }
    if (axis == 1) {
        return DD_GREEN;
    }
    return DD_BLUE;
}

static f32 gizmo_handle_length(const Camera* cam, Vec3 pos)
{
    return vec3_distance(cam->pos, pos) * GIZMO_HANDLE_FRACTION;
}

static f32 gizmo_seg_dist(Vec2 p, Vec2 a, Vec2 b)
{
    Vec2 ab = vec2_sub(b, a);
    Vec2 ap = vec2_sub(p, a);
    f32 denom = vec2_dot(ab, ab);
    f32 t = denom > 1e-6f ? f_clamp01(vec2_dot(ap, ab) / denom) : 0.0f;
    Vec2 proj = vec2_add(a, vec2_scale(ab, t));
    return vec2_length(vec2_sub(p, proj));
}

static i32 gizmo_pick_axis(const Gizmo* gizmo, Vec3 pos, f32 handle_len, Vec2 mouse)
{
    Vec2 origin_s;
    if (!r_project_to_screen(pos, &origin_s)) {
        return -1;
    }
    if (vec2_length(vec2_sub(mouse, origin_s)) < GIZMO_DEAD_ZONE_PIXELS) {
        return -1;
    }
    f32 best = GIZMO_PICK_PIXELS;
    i32 picked = -1;
    for (i32 axis = 0; axis < 3; axis++) {
        Vec3 tip = vec3_add(pos, vec3_scale(gizmo_axis_dir(gizmo, axis), handle_len));
        Vec2 tip_s;
        if (!r_project_to_screen(tip, &tip_s)) {
            continue;
        }
        f32 d = gizmo_seg_dist(mouse, origin_s, tip_s);
        if (d < best) {
            best = d;
            picked = axis;
        }
    }
    return picked;
}

static b32 gizmo_pick_ring(const Gizmo* gizmo, Vec3 pos, f32 radius, Vec2 mouse)
{
    Vec2 prev = v2(0.0f, 0.0f);
    b32 have_prev = 0;
    f32 best = 1e9f;
    for (i32 i = 0; i <= 32; i++) {
        f32 angle = (f32)i / 32.0f * 2.0f * PI32;
        Vec3 rim = quat_rotate_vec3(gizmo->basis,
                                    v3(cosf(angle) * radius, 0.0f, sinf(angle) * radius));
        Vec3 p = vec3_add(pos, rim);
        Vec2 s;
        if (!r_project_to_screen(p, &s)) {
            have_prev = 0;
            continue;
        }
        if (have_prev) {
            f32 d = gizmo_seg_dist(mouse, prev, s);
            best = f_min(best, d);
        }
        prev = s;
        have_prev = 1;
    }
    return best < GIZMO_PICK_PIXELS;
}

static f32 gizmo_snap(f32 value, f32 step)
{
    if (step <= 0.0f) {
        return value;
    }
    return floorf(value / step + 0.5f) * step;
}

void gizmo_init(Gizmo* gizmo)
{
    gizmo->mode = GIZMO_TRANSLATE;
    gizmo->basis = quat_identity();
    gizmo->active_axis = -1;
    gizmo->dragging = 0;
    gizmo->drag_start_param = 0.0f;
    gizmo->drag_delta = 0.0f;
    gizmo->start_pos = vec3_zero();
    gizmo->start_yaw = 0.0f;
    gizmo->start_scale = 1.0f;
}

b32 gizmo_update(Gizmo* gizmo, const struct GameInput* input, const Camera* cam, Vec2 viewport,
                 Vec3* pos, f32* yaw, f32* scale, f32 snap_pos, f32 snap_ang, f32 snap_scale)
{
    Vec2 mouse = v2(input->mouse_x, input->mouse_y);
    Ray ray = camera_mouse_ray(cam, mouse.x, mouse.y, viewport);
    f32 handle_len = gizmo_handle_length(cam, *pos);

    if (!gizmo->dragging) {
        if (input->mouse_pressed[MOUSE_LEFT] && !input->mouse_down[MOUSE_RIGHT]) {
            i32 axis = -1;
            if (gizmo->mode == GIZMO_ROTATE) {
                if (yaw && gizmo_pick_ring(gizmo, *pos, handle_len, mouse)) {
                    axis = 1;
                }
            } else if (gizmo->mode != GIZMO_SCALE || scale) {
                axis = gizmo_pick_axis(gizmo, *pos, handle_len, mouse);
            }
            if (axis >= 0) {
                gizmo->dragging = 1;
                gizmo->active_axis = axis;
                gizmo->start_pos = *pos;
                gizmo->start_yaw = yaw ? *yaw : 0.0f;
                gizmo->start_scale = scale ? *scale : 1.0f;
                gizmo->drag_delta = 0.0f;
                gizmo->drag_start_param =
                    closest_point_on_line_to_ray(*pos, gizmo_axis_dir(gizmo, axis), ray);
                return 1;
            }
        }
        return 0;
    }

    if (!input->mouse_down[MOUSE_LEFT]) {
        gizmo->dragging = 0;
        gizmo->active_axis = -1;
        return 1;
    }

    Vec3 axis_dir = gizmo_axis_dir(gizmo, gizmo->active_axis);
    if (gizmo->mode == GIZMO_TRANSLATE) {
        f32 s = closest_point_on_line_to_ray(gizmo->start_pos, axis_dir, ray);
        f32 delta = s - gizmo->drag_start_param;
        Vec3 next = vec3_add(gizmo->start_pos, vec3_scale(axis_dir, delta));
        next.x = gizmo_snap(next.x, snap_pos);
        next.y = gizmo_snap(next.y, snap_pos);
        next.z = gizmo_snap(next.z, snap_pos);
        *pos = next;
    } else if (gizmo->mode == GIZMO_ROTATE && yaw) {
        gizmo->drag_delta += input->mouse_dx * 0.01f;
        f32 next = gizmo->start_yaw + gizmo->drag_delta;
        if (snap_ang > 0.0f) {
            next = gizmo_snap(next, snap_ang * DEG_TO_RAD);
        }
        *yaw = f_wrap_angle(next);
    } else if (gizmo->mode == GIZMO_SCALE && scale) {
        gizmo->drag_delta += input->mouse_dx * 0.01f;
        f32 next = gizmo_snap(gizmo->start_scale + gizmo->drag_delta, snap_scale);
        *scale = f_clamp(next, 0.02f, 100.0f);
    }
    return 1;
}

void gizmo_render(const Gizmo* gizmo, const Camera* cam, Vec3 pos)
{
    f32 handle_len = gizmo_handle_length(cam, pos);
    dd_overlay(1);
    if (gizmo->mode == GIZMO_ROTATE) {
        b32 hot = gizmo->dragging && gizmo->active_axis == 1;
        Vec3 up = quat_rotate_vec3(gizmo->basis, v3(0.0f, 1.0f, 0.0f));
        dd_circle(pos, up, handle_len, gizmo_axis_color(1, hot));
    } else {
        for (i32 axis = 0; axis < 3; axis++) {
            b32 hot = gizmo->dragging && gizmo->active_axis == axis;
            Vec3 tip = vec3_add(pos, vec3_scale(gizmo_axis_dir(gizmo, axis), handle_len));
            dd_arrow(pos, tip, handle_len * 0.12f, gizmo_axis_color(axis, hot));
        }
    }
    dd_overlay(0);
}
