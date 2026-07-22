#include "render/camera.h"
#include "platform/platform.h"

#define CAM_PITCH_LIMIT (89.0f * DEG_TO_RAD)
#define CAM_MOUSE_SENSITIVITY 0.0022f
#define CAM_FAST_MULTIPLIER 4.0f
#define CAM_MIN_SPEED 0.5f
#define CAM_MAX_SPEED 200.0f

void camera_init(Camera* cam, Vec3 pos)
{
    cam->pos = pos;
    cam->yaw = 0.0f;
    cam->pitch = 0.0f;
    cam->fov_y = 70.0f * DEG_TO_RAD;
    cam->znear = 0.05f;
    cam->zfar = 2000.0f;
    cam->move_speed = 10.0f;
}

void camera_look_at(Camera* cam, Vec3 target)
{
    Vec3 d = vec3_normalize(vec3_sub(target, cam->pos));
    cam->yaw = atan2f(d.x, -d.z);
    cam->pitch = asinf(f_clamp(d.y, -1.0f, 1.0f));
}

Vec3 camera_forward(const Camera* cam)
{
    f32 cp = cosf(cam->pitch);
    return v3(cp * sinf(cam->yaw), sinf(cam->pitch), -cp * cosf(cam->yaw));
}

Vec3 camera_right(const Camera* cam)
{
    return vec3_normalize(vec3_cross(camera_forward(cam), v3(0.0f, 1.0f, 0.0f)));
}

Mat4 camera_view(const Camera* cam)
{
    return mat4_look_at(cam->pos, vec3_add(cam->pos, camera_forward(cam)), v3(0.0f, 1.0f, 0.0f));
}

Mat4 camera_proj(const Camera* cam, f32 aspect)
{
    return mat4_perspective(cam->fov_y, aspect, cam->znear, cam->zfar);
}

Ray camera_mouse_ray(const Camera* cam, f32 mouse_x, f32 mouse_y, Vec2 viewport)
{
    f32 ndc_x = mouse_x / viewport.x * 2.0f - 1.0f;
    f32 ndc_y = 1.0f - mouse_y / viewport.y * 2.0f;
    f32 tan_half = tanf(cam->fov_y * 0.5f);
    f32 aspect = viewport.x / viewport.y;
    Vec3 forward = camera_forward(cam);
    Vec3 right = camera_right(cam);
    Vec3 up = vec3_cross(right, forward);
    Ray ray;
    ray.origin = cam->pos;
    ray.dir = vec3_normalize(vec3_add(forward,
                                      vec3_add(vec3_scale(right, ndc_x * tan_half * aspect),
                                               vec3_scale(up, ndc_y * tan_half))));
    return ray;
}

void camera_fly_update(Camera* cam, const struct GameInput* input, f32 dt)
{
    const GameInput* in = input;

    if (in->mouse_down[MOUSE_RIGHT]) {
        platform_set_cursor_captured(1);
        cam->yaw = f_wrap_angle(cam->yaw + in->mouse_dx * CAM_MOUSE_SENSITIVITY);
        cam->pitch = f_clamp(cam->pitch - in->mouse_dy * CAM_MOUSE_SENSITIVITY, -CAM_PITCH_LIMIT, CAM_PITCH_LIMIT);
    } else {
        platform_set_cursor_captured(0);
    }

    if (in->scroll_dy != 0.0f) {
        cam->move_speed = f_clamp(cam->move_speed * powf(1.25f, in->scroll_dy), CAM_MIN_SPEED, CAM_MAX_SPEED);
    }

    Vec3 wish = vec3_zero();
    Vec3 forward = camera_forward(cam);
    Vec3 right = camera_right(cam);
    if (in->key_down[KEY_W]) { wish = vec3_add(wish, forward); }
    if (in->key_down[KEY_S]) { wish = vec3_sub(wish, forward); }
    if (in->key_down[KEY_D]) { wish = vec3_add(wish, right); }
    if (in->key_down[KEY_A]) { wish = vec3_sub(wish, right); }
    if (in->key_down[KEY_E]) { wish = vec3_add(wish, v3(0.0f, 1.0f, 0.0f)); }
    if (in->key_down[KEY_Q]) { wish = vec3_sub(wish, v3(0.0f, 1.0f, 0.0f)); }

    f32 speed = cam->move_speed;
    if (in->key_down[KEY_LEFT_SHIFT]) {
        speed *= CAM_FAST_MULTIPLIER;
    }
    cam->pos = vec3_add(cam->pos, vec3_scale(vec3_normalize(wish), speed * dt));
}
