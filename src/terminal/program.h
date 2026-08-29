#pragma once

#include "core/types.h"
#include "terminal/term_view.h"

namespace anom {

class Screen;
class Shell;

enum class TermKey : u32 {
    Enter,
    Escape,
    Backspace,
    Delete,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    Quit,
};

struct ProgramContext {
    Shell* shell = nullptr;
    Screen* screen = nullptr;
    TermScene* scene = nullptr;
    TermRequest* request = nullptr;
    f32 blink = 0.0f;
};

class Program {
public:
    virtual ~Program() = default;

    virtual void enter(const ProgramContext& ctx, const TermView& view) { (void)ctx; (void)view; }
    virtual void update(const ProgramContext& ctx, const TermView& view, f32 dt) = 0;
    virtual bool key(const ProgramContext& ctx, const TermView& view, TermKey key)
    {
        (void)ctx;
        (void)view;
        return key == TermKey::Quit || key == TermKey::Escape;
    }
    virtual void key_char(const ProgramContext& ctx, const TermView& view, char c)
    {
        (void)ctx;
        (void)view;
        (void)c;
    }
    virtual f32 pixelate() const { return 1.0f; }
    virtual bool wants_exit() const { return false; }
    virtual void exit(const ProgramContext& ctx) { (void)ctx; }
};

} // namespace anom
