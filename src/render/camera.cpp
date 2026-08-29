#include "render/camera.h"
#include "platform/input.h"

namespace anom {
namespace {

constexpr f32 kPitchLimit = 89.0f * kDegToRad;
constexpr f32 kMouseSensitivity = 0.0022f;
constexpr f32 kFastMultiplier = 4.0f;
constexpr f32 kMinSpeed = 0.5f;
constexpr f32 kMaxSpeed = 200.0f;

} // namespace

void Camera::look_at(Vec3 target)
{
    const Vec3 d = normalize(target - pos);
    yaw = std::atan2(d.x, -d.z);
    pitch = std::asin(f_clamp(d.y, -1.0f, 1.0f));
}

Vec3 Camera::forward() const
{
    const f32 cp = std::cos(pitch);
    return {cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw)};
}

Vec3 Camera::right() const
{
    return normalize(cross(forward(), Vec3{0.0f, 1.0f, 0.0f}));
}

Vec3 Camera::up() const
{
    return cross(right(), forward());
}

Mat4 Camera::view() const
{
    return mat4_look_at(pos, pos + forward(), Vec3{0.0f, 1.0f, 0.0f});
}

Mat4 Camera::proj(f32 aspect) const
{
    return mat4_perspective(fov_y, aspect, znear, zfar);
}

Ray Camera::mouse_ray(Vec2 mouse, Vec2 viewport) const
{
    const f32 ndc_x = mouse.x / viewport.x * 2.0f - 1.0f;
    const f32 ndc_y = 1.0f - mouse.y / viewport.y * 2.0f;
    const f32 tan_half = std::tan(fov_y * 0.5f);
    const f32 aspect = viewport.x / viewport.y;

    const Vec3 f = forward();
    const Vec3 r = right();
    const Vec3 u = cross(r, f);

    Ray ray;
    ray.origin = pos;
    ray.dir = normalize(f + r * (ndc_x * tan_half * aspect) + u * (ndc_y * tan_half));
    return ray;
}

bool Camera::project_to_screen(Vec3 world, Vec2 viewport, Vec2& out_screen) const
{
    const Vec3 to_point = world - pos;
    const Vec3 f = forward();
    const f32 depth = dot(to_point, f);
    if (depth <= znear) {
        return false;
    }

    const Vec3 r = right();
    const Vec3 u = cross(r, f);
    const f32 tan_half = std::tan(fov_y * 0.5f);
    const f32 aspect = viewport.x / viewport.y;

    const f32 ndc_x = dot(to_point, r) / (depth * tan_half * aspect);
    const f32 ndc_y = dot(to_point, u) / (depth * tan_half);

    out_screen.x = (ndc_x * 0.5f + 0.5f) * viewport.x;
    out_screen.y = (0.5f - ndc_y * 0.5f) * viewport.y;
    return true;
}

bool Camera::fly_update(const Input& input, f32 dt)
{
    const bool capture = input.down(MouseButton::Right);
    if (capture) {
        yaw = f_wrap_angle(yaw + input.mouse_delta().x * kMouseSensitivity);
        pitch = f_clamp(pitch - input.mouse_delta().y * kMouseSensitivity,
                        -kPitchLimit, kPitchLimit);
    }

    if (input.scroll() != 0.0f) {
        move_speed = f_clamp(move_speed * std::pow(1.25f, input.scroll()), kMinSpeed, kMaxSpeed);
    }

    Vec3 wish{0.0f, 0.0f, 0.0f};
    const Vec3 f = forward();
    const Vec3 r = right();
    if (input.down(Key::W)) { wish += f; }
    if (input.down(Key::S)) { wish -= f; }
    if (input.down(Key::D)) { wish += r; }
    if (input.down(Key::A)) { wish -= r; }
    if (input.down(Key::E)) { wish += Vec3{0.0f, 1.0f, 0.0f}; }
    if (input.down(Key::Q)) { wish -= Vec3{0.0f, 1.0f, 0.0f}; }

    f32 speed = move_speed;
    if (input.down(Key::LeftShift)) {
        speed *= kFastMultiplier;
    }
    pos += normalize(wish) * (speed * dt);
    return capture;
}

} // namespace anom
