#include "test.h"

#include "app/drive_controls.h"
#include "platform/input.h"

using namespace anom;

namespace {
constexpr f32 kDt = 1.0f / 120.0f;

struct PadFrame {
    f32 axes[kPadAxisCount] = {0.0f, 0.0f, 0.0f, 0.0f, -1.0f, -1.0f};
    u8 buttons[kPadButtonCount] = {};
};

void feed(Input& input, const PadFrame& frame)
{
    input.begin_frame();
    input.set_pad(true, frame.axes, frame.buttons);
    input.finish_frame();
}
}

TEST(input, pad_axis_shaping_has_a_deadzone_and_expo)
{
    CHECK(pad_shape(0.0f) == 0.0f);
    CHECK(pad_shape(0.1f) == 0.0f);
    CHECK(pad_shape(-0.1f) == 0.0f);
    CHECK_NEAR(pad_shape(1.0f), 1.0f, 1e-5);
    CHECK_NEAR(pad_shape(-1.0f), -1.0f, 1e-5);
    const f32 half = pad_shape(0.5f);
    CHECK(half > 0.1f);
    CHECK(half < 0.5f);
    CHECK_NEAR(pad_shape(-0.5f), -half, 1e-6);
    CHECK(pad_shape(0.8f) > half);
}

TEST(input, pad_triggers_rest_at_zero_and_reach_one)
{
    CHECK(pad_trigger(-1.0f) == 0.0f);
    CHECK(pad_trigger(-0.95f) == 0.0f);
    CHECK_NEAR(pad_trigger(1.0f), 1.0f, 1e-5);
    CHECK(pad_trigger(0.0f) > 0.4f);
    CHECK(pad_trigger(0.0f) < 0.6f);
}

TEST(input, pad_buttons_report_edges_per_frame)
{
    Input input;
    PadFrame frame;
    feed(input, frame);
    CHECK(input.pad_connected());
    CHECK(!input.pad_down(PadButton::A));

    frame.buttons[static_cast<i32>(PadButton::A)] = 1;
    feed(input, frame);
    CHECK(input.pad_down(PadButton::A));
    CHECK(input.pad_pressed(PadButton::A));
    CHECK(!input.pad_released(PadButton::A));

    feed(input, frame);
    CHECK(input.pad_down(PadButton::A));
    CHECK(!input.pad_pressed(PadButton::A));

    frame.buttons[static_cast<i32>(PadButton::A)] = 0;
    feed(input, frame);
    CHECK(!input.pad_down(PadButton::A));
    CHECK(input.pad_released(PadButton::A));

    input.begin_frame();
    input.set_pad(false, nullptr, nullptr);
    input.finish_frame();
    CHECK(!input.pad_connected());
    CHECK(input.pad_axis(PadAxis::LeftX) == 0.0f);
}

TEST(controls, the_right_trigger_is_throttle_and_the_left_stick_steers)
{
    Input input;
    PadFrame frame;
    frame.axes[static_cast<i32>(PadAxis::LeftX)] = 1.0f;
    frame.axes[static_cast<i32>(PadAxis::RightTrigger)] = 1.0f;
    frame.axes[static_cast<i32>(PadAxis::LeftTrigger)] = 0.0f;
    feed(input, frame);

    PadTapState taps;
    DriveIntent intent;
    drive_intent_from_pad(input, kDt, taps, intent);
    CHECK_NEAR(intent.steer, 1.0f, 1e-5);
    CHECK_NEAR(intent.throttle, 1.0f, 1e-5);
    CHECK(intent.reverse > 0.4f);
    CHECK(intent.reverse < 0.6f);
    CHECK(!intent.handbrake);

    Input idle;
    DriveIntent none;
    drive_intent_from_pad(idle, kDt, taps, none);
    CHECK(none.steer == 0.0f);
    CHECK(none.throttle == 0.0f);
}

TEST(controls, a_short_b_press_is_a_handbrake_tap_and_a_long_one_is_momentary)
{
    Input input;
    PadFrame frame;
    PadTapState taps;
    DriveIntent intent;

    frame.buttons[static_cast<i32>(PadButton::B)] = 1;
    feed(input, frame);
    drive_intent_from_pad(input, kDt, taps, intent);
    CHECK(intent.handbrake);
    CHECK(!intent.handbrake_tap);
    frame.buttons[static_cast<i32>(PadButton::B)] = 0;
    feed(input, frame);
    drive_intent_from_pad(input, kDt, taps, intent);
    CHECK(!intent.handbrake);
    CHECK(intent.handbrake_tap);

    frame.buttons[static_cast<i32>(PadButton::B)] = 1;
    for (i32 i = 0; i < 60; i++) {
        feed(input, frame);
        drive_intent_from_pad(input, kDt, taps, intent);
        CHECK(intent.handbrake);
    }
    frame.buttons[static_cast<i32>(PadButton::B)] = 0;
    feed(input, frame);
    drive_intent_from_pad(input, kDt, taps, intent);
    CHECK(!intent.handbrake_tap);
}

TEST(controls, on_foot_the_left_stick_walks_and_a_walks_nowhere_but_jumps)
{
    Input input;
    PadFrame frame;
    frame.axes[static_cast<i32>(PadAxis::LeftY)] = -1.0f;
    frame.buttons[static_cast<i32>(PadButton::A)] = 1;
    feed(input, frame);

    FootIntent intent;
    foot_intent_from_pad(input, kDt, intent);
    CHECK_NEAR(intent.move.y, 1.0f, 1e-5);
    CHECK(intent.move.x == 0.0f);
    CHECK(intent.jump);
    CHECK(!intent.use_pressed);
}
