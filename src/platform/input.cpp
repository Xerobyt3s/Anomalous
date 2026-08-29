#include "platform/input.h"

#include <cstring>

namespace anom {

void Input::begin_frame()
{
    std::memset(key_pressed_, 0, sizeof(key_pressed_));
    std::memset(key_released_, 0, sizeof(key_released_));
    std::memset(mouse_pressed_, 0, sizeof(mouse_pressed_));
    std::memset(mouse_released_, 0, sizeof(mouse_released_));
    char_count_ = 0;
    scroll_dy_ = 0.0f;
    prev_mouse_x_ = mouse_x_;
    prev_mouse_y_ = mouse_y_;
}

void Input::set_key(i32 key, bool is_down)
{
    if (key < 0 || key >= kMaxKeys) {
        return;
    }
    if (is_down) {
        key_down_[key] = 1;
        key_pressed_[key] = 1;
    } else {
        key_down_[key] = 0;
        key_released_[key] = 1;
    }
}

void Input::set_mouse_button(i32 button, bool is_down)
{
    if (button < 0 || button >= kMaxMouseButtons) {
        return;
    }
    if (is_down) {
        mouse_down_[button] = 1;
        mouse_pressed_[button] = 1;
    } else {
        mouse_down_[button] = 0;
        mouse_released_[button] = 1;
    }
}

void Input::set_mouse_pos(f32 x, f32 y)
{
    mouse_x_ = x;
    mouse_y_ = y;
}

void Input::add_scroll(f32 dy)
{
    scroll_dy_ += dy;
}

void Input::push_char(u32 codepoint)
{
    if (char_count_ < kMaxCharsPerFrame) {
        chars_[char_count_++] = codepoint;
    }
}

void Input::resync_mouse()
{
    first_mouse_sample_ = true;
}

void Input::finish_frame()
{
    if (first_mouse_sample_) {
        prev_mouse_x_ = mouse_x_;
        prev_mouse_y_ = mouse_y_;
        first_mouse_sample_ = false;
    }
    mouse_dx_ = mouse_x_ - prev_mouse_x_;
    mouse_dy_ = mouse_y_ - prev_mouse_y_;
}

} // namespace anom
