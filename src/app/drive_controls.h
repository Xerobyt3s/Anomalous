#pragma once

#include "core/types.h"
#include "math/vmath.h"
#include "platform/input.h"

namespace anom {

inline constexpr f32 kPadDeadzone = 0.14f;
inline constexpr f32 kPadExpo = 1.6f;
inline constexpr f32 kPadTriggerDeadzone = 0.05f;
inline constexpr f32 kPadLookRate = 2.6f;
inline constexpr f32 kPadTapTime = 0.3f;

f32 pad_shape(f32 raw, f32 deadzone = kPadDeadzone, f32 expo = kPadExpo);
f32 pad_trigger(f32 raw);

struct PadTapState {
    f32 handbrake_held = 0.0f;
    f32 ignition_held = 0.0f;
};

struct DriveIntent {
    f32 steer = 0.0f;
    f32 throttle = 0.0f;
    f32 reverse = 0.0f;
    bool handbrake = false;
    bool crank = false;
    bool horn = false;
    bool use_down = false;
    bool use_pressed = false;
    bool handbrake_tap = false;
    bool ignition_tap = false;
    bool headlights = false;
    bool manual = false;
    bool shift_up = false;
    bool shift_down = false;
    bool chase = false;
    bool look_behind = false;
    bool menu = false;
    Vec2 look;
};

struct FootIntent {
    Vec2 move;
    Vec2 look;
    bool jump = false;
    bool sprint = false;
    bool crouch = false;
    bool use_down = false;
    bool use_pressed = false;
    bool menu = false;
};

void drive_intent_from_pad(const Input& input, f32 dt, PadTapState& taps, DriveIntent& out);
void foot_intent_from_pad(const Input& input, f32 dt, FootIntent& out);

} // namespace anom
