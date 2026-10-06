#include "app/drive_controls.h"

#include <cmath>

namespace anom {

f32 pad_shape(f32 raw, f32 deadzone, f32 expo)
{
    const f32 mag = f_abs(raw);
    if (mag <= deadzone) {
        return 0.0f;
    }
    const f32 t = f_clamp01((mag - deadzone) / f_max(1.0f - deadzone, 1e-3f));
    const f32 shaped = std::pow(t, expo);
    return raw < 0.0f ? -shaped : shaped;
}

f32 pad_trigger(f32 raw)
{
    const f32 t = f_clamp01((raw + 1.0f) * 0.5f);
    return t <= kPadTriggerDeadzone ? 0.0f : (t - kPadTriggerDeadzone) / (1.0f - kPadTriggerDeadzone);
}

void drive_intent_from_pad(const Input& input, f32 dt, PadTapState& taps, DriveIntent& out)
{
    out = DriveIntent{};
    if (!input.pad_connected()) {
        taps = PadTapState{};
        return;
    }
    out.steer = pad_shape(input.pad_axis(PadAxis::LeftX));
    out.throttle = pad_trigger(input.pad_axis(PadAxis::RightTrigger));
    out.reverse = pad_trigger(input.pad_axis(PadAxis::LeftTrigger));
    out.look = Vec2{pad_shape(input.pad_axis(PadAxis::RightX)) * kPadLookRate * dt,
                    pad_shape(input.pad_axis(PadAxis::RightY)) * kPadLookRate * dt};

    if (input.pad_pressed(PadButton::B)) {
        taps.handbrake_held = 0.0f;
    }
    if (input.pad_down(PadButton::B)) {
        taps.handbrake_held += dt;
    }
    out.handbrake = input.pad_down(PadButton::B);
    out.handbrake_tap = input.pad_released(PadButton::B) && taps.handbrake_held < kPadTapTime;

    if (input.pad_pressed(PadButton::Y)) {
        taps.ignition_held = 0.0f;
    }
    if (input.pad_down(PadButton::Y)) {
        taps.ignition_held += dt;
    }
    out.crank = input.pad_down(PadButton::Y);
    out.ignition_tap = input.pad_released(PadButton::Y) && taps.ignition_held < kPadTapTime;

    out.horn = input.pad_down(PadButton::RightThumb);
    out.use_down = input.pad_down(PadButton::A);
    out.use_pressed = input.pad_pressed(PadButton::A);
    out.headlights = input.pad_pressed(PadButton::X);
    out.manual = input.pad_pressed(PadButton::LeftThumb);
    out.shift_up = input.pad_pressed(PadButton::RightBumper);
    out.shift_down = input.pad_pressed(PadButton::LeftBumper);
    out.chase = input.pad_pressed(PadButton::DpadUp);
    out.look_behind = input.pad_down(PadButton::Back);
    out.menu = input.pad_pressed(PadButton::Start);
}

void foot_intent_from_pad(const Input& input, f32 dt, FootIntent& out)
{
    out = FootIntent{};
    if (!input.pad_connected()) {
        return;
    }
    out.move = Vec2{pad_shape(input.pad_axis(PadAxis::LeftX)), -pad_shape(input.pad_axis(PadAxis::LeftY))};
    out.look = Vec2{pad_shape(input.pad_axis(PadAxis::RightX)) * kPadLookRate * dt,
                    pad_shape(input.pad_axis(PadAxis::RightY)) * kPadLookRate * dt};
    out.jump = input.pad_pressed(PadButton::A);
    out.sprint = pad_trigger(input.pad_axis(PadAxis::RightTrigger)) > 0.5f;
    out.crouch = pad_trigger(input.pad_axis(PadAxis::LeftTrigger)) > 0.5f;
    out.use_down = input.pad_down(PadButton::X);
    out.use_pressed = input.pad_pressed(PadButton::X);
    out.menu = input.pad_pressed(PadButton::Start);
}

} // namespace anom
