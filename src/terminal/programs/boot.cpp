#include "terminal/programs/boot.h"
#include "terminal/screen.h"
#include "terminal/shell.h"

#include <cmath>
#include <cstring>

namespace anom {

void BootProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    timer_ = 0.0f;
}

void BootProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)view;
    timer_ += dt;

    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_text(3, 26, TC_BRIGHT, "D R - O S   2 . 2");
    s.grid_text(5, 22, TC_DIM, "REDLINE SYSTEMS  (C) 1988");

    if (timer_ > 1.0f) {
        s.grid_text(8, 6, TC_GREEN, "MEMORY TEST ........ 640K OK");
    }
    if (timer_ > 1.7f) {
        s.grid_text(9, 6, TC_GREEN, "BUS 12V ............ OK");
    }
    if (timer_ > 2.3f) {
        s.grid_text(10, 6, TC_GREEN, "SENSOR LOOM ........ OK");
    }
    if (timer_ > 2.9f) {
        s.grid_text(11, 6, TC_GREEN, "DRIVE A: ........... 354K FIXED, %uK FREE",
                    ctx.shell->fs().free_bytes(kFsDriveA) / 1024);
    }
    if (timer_ > 3.5f) {
        s.grid_text(12, 6, TC_AMBER, "UPLINK ............. NO CARRIER");
    }
    if (timer_ > 4.1f) {
        s.grid_text(14, 6, TC_BRIGHT, "READY. TYPE HELP FOR COMMANDS.");
    }
}

bool BootProgram::key(const ProgramContext& ctx, const TermView& view, TermKey key)
{
    (void)ctx;
    (void)view;
    (void)key;
    return false;
}

void ShellProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)view;
    (void)dt;

    Shell& shell = *ctx.shell;
    Screen& s = *ctx.screen;
    s.grid_clear();

    const i32 rows_for_log = static_cast<i32>(kTermRows) - 1;
    const i32 total = static_cast<i32>(s.line_count()) + (s.out_line().empty() ? 0 : 1);
    const i32 first = total > rows_for_log ? total - rows_for_log : 0;

    i32 row = 0;
    for (i32 i = first; i < total && row < rows_for_log; i++, row++) {
        const std::string_view text = i < static_cast<i32>(s.line_count())
                                        ? s.line(static_cast<u32>(i))
                                        : s.out_line();
        s.grid_text(row, 0, TC_GREEN, "%.*s", static_cast<int>(text.size()), text.data());
    }

    char prompt[kTermPromptMax];
    shell.prompt(prompt, sizeof(prompt));
    const std::string_view input = shell.input();
    s.grid_text(row, 0, TC_BRIGHT, "%s%.*s", prompt, static_cast<int>(input.size()),
                input.data());

    if (s.idle() && std::fmod(ctx.blink, 1.06f) < 0.53f) {
        i32 col = static_cast<i32>(std::strlen(prompt)) + static_cast<i32>(shell.cursor());
        if (col > static_cast<i32>(kTermCols) - 1) {
            col = static_cast<i32>(kTermCols) - 1;
        }
        s.grid_put(row, col, 127, TC_BRIGHT);
    }
}

bool ShellProgram::key(const ProgramContext& ctx, const TermView& view, TermKey key)
{
    (void)view;
    Shell& shell = *ctx.shell;
    switch (key) {
    case TermKey::Enter:
        shell.key(ShellKey::Enter);
        break;
    case TermKey::Backspace:
        shell.key(ShellKey::Backspace);
        break;
    case TermKey::Delete:
        shell.key(ShellKey::Delete);
        break;
    case TermKey::Left:
        shell.key(ShellKey::Left);
        break;
    case TermKey::Right:
        shell.key(ShellKey::Right);
        break;
    case TermKey::Home:
        shell.key(ShellKey::Home);
        break;
    case TermKey::End:
        shell.key(ShellKey::End);
        break;
    case TermKey::Up:
        shell.key(ShellKey::Up);
        break;
    case TermKey::Down:
        shell.key(ShellKey::Down);
        break;
    default:
        break;
    }
    return false;
}

void ShellProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)view;
    ctx.shell->key_char(c);
}

} // namespace anom
