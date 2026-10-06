#pragma once

#include "core/types.h"
#include "math/vmath.h"

#include <span>

namespace anom {

enum class Key : i32 {
    None = -1,
    Space = 32,
    Apostrophe = 39,
    Comma = 44,
    Minus = 45,
    Period = 46,
    Slash = 47,
    Num0 = 48, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Semicolon = 59,
    Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91,
    Backslash = 92,
    RightBracket = 93,
    Grave = 96,
    Escape = 256,
    Enter = 257,
    Tab = 258,
    Backspace = 259,
    Insert = 260,
    Delete = 261,
    Right = 262,
    Left = 263,
    Down = 264,
    Up = 265,
    PageUp = 266,
    PageDown = 267,
    Home = 268,
    End = 269,
    CapsLock = 280,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    LeftShift = 340,
    LeftControl = 341,
    LeftAlt = 342,
    RightShift = 344,
    RightControl = 345,
    RightAlt = 346,
};

enum class MouseButton : i32 {
    Left = 0,
    Right = 1,
    Middle = 2,
};

enum class PadAxis : i32 {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

enum class PadButton : i32 {
    A = 0,
    B,
    X,
    Y,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftThumb,
    RightThumb,
    DpadUp,
    DpadRight,
    DpadDown,
    DpadLeft,
};

inline constexpr i32 kPadAxisCount = 6;
inline constexpr i32 kPadButtonCount = 15;

class Input {
public:
    static constexpr i32 kMaxKeys = 512;
    static constexpr i32 kMaxMouseButtons = 8;
    static constexpr u32 kMaxCharsPerFrame = 64;

    bool down(Key key) const { return read(key_down_, key); }
    bool pressed(Key key) const { return read(key_pressed_, key); }
    bool released(Key key) const { return read(key_released_, key); }

    bool down(MouseButton b) const { return read(mouse_down_, b); }
    bool pressed(MouseButton b) const { return read(mouse_pressed_, b); }
    bool released(MouseButton b) const { return read(mouse_released_, b); }

    Vec2 mouse_pos() const { return {mouse_x_, mouse_y_}; }
    Vec2 mouse_delta() const { return {mouse_dx_, mouse_dy_}; }
    f32 scroll() const { return scroll_dy_; }

    std::span<const u32> chars() const { return {chars_, char_count_}; }

    bool pad_connected() const { return pad_connected_; }
    f32 pad_axis(PadAxis axis) const { return pad_axes_[static_cast<i32>(axis)]; }
    bool pad_down(PadButton b) const { return pad_down_[static_cast<i32>(b)] != 0; }
    bool pad_pressed(PadButton b) const { return pad_pressed_[static_cast<i32>(b)] != 0; }
    bool pad_released(PadButton b) const { return pad_released_[static_cast<i32>(b)] != 0; }
    void set_pad(bool connected, const f32* axes, const u8* buttons);

    void begin_frame();
    void set_key(i32 key, bool is_down);
    void set_mouse_button(i32 button, bool is_down);
    void set_mouse_pos(f32 x, f32 y);
    void add_scroll(f32 dy);
    void push_char(u32 codepoint);
    void resync_mouse();
    void finish_frame();

private:
    static bool read(const u8* table, Key key)
    {
        const i32 index = static_cast<i32>(key);
        return index >= 0 && index < kMaxKeys && table[index] != 0;
    }

    static bool read(const u8* table, MouseButton button)
    {
        const i32 index = static_cast<i32>(button);
        return index >= 0 && index < kMaxMouseButtons && table[index] != 0;
    }

    u8 key_down_[kMaxKeys] = {};
    u8 key_pressed_[kMaxKeys] = {};
    u8 key_released_[kMaxKeys] = {};
    u8 mouse_down_[kMaxMouseButtons] = {};
    u8 mouse_pressed_[kMaxMouseButtons] = {};
    u8 mouse_released_[kMaxMouseButtons] = {};
    u32 chars_[kMaxCharsPerFrame] = {};
    u32 char_count_ = 0;
    f32 mouse_x_ = 0.0f;
    f32 mouse_y_ = 0.0f;
    f32 mouse_dx_ = 0.0f;
    f32 mouse_dy_ = 0.0f;
    f32 prev_mouse_x_ = 0.0f;
    f32 prev_mouse_y_ = 0.0f;
    f32 scroll_dy_ = 0.0f;
    bool first_mouse_sample_ = true;
    bool pad_connected_ = false;
    f32 pad_axes_[kPadAxisCount] = {};
    u8 pad_down_[kPadButtonCount] = {};
    u8 pad_pressed_[kPadButtonCount] = {};
    u8 pad_released_[kPadButtonCount] = {};
};

} // namespace anom
