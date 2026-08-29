#pragma once

#include "core/fixed_string.h"
#include "terminal/program.h"

namespace anom {

class Mailbox;
struct CommsMsg;

class CommsProgram : public Program {
public:
    explicit CommsProgram(Mailbox& mail) : mail_(&mail) {}

    void enter(const ProgramContext& ctx, const TermView& view) override;
    void update(const ProgramContext& ctx, const TermView& view, f32 dt) override;
    bool key(const ProgramContext& ctx, const TermView& view, TermKey key) override;
    void key_char(const ProgramContext& ctx, const TermView& view, char c) override;
    f32 pixelate() const override;

private:
    enum class Phase : u32 {
        Connect,
        Inbox,
        Read,
        Brief,
    };

    void set_phase(Phase phase);
    void set_status(std::string_view text);
    u32 connect_lines(FixedString<64>* out) const;

    void draw_connect(const ProgramContext& ctx, f32 dt);
    void draw_inbox(const ProgramContext& ctx);
    void draw_read(const ProgramContext& ctx);
    void draw_brief(const ProgramContext& ctx, f32 dt);
    static void frame(Screen& s, i32 row0, i32 col0, i32 cols, i32 rows, i32 title_cols);

    Mailbox* mail_ = nullptr;

    Phase phase_ = Phase::Connect;
    f32 phase_time_ = 0.0f;
    f32 materialize_ = 0.0f;
    f32 reveal_ = 0.0f;
    i32 open_ = 0;
    i32 page_ = 0;
    i32 blips_ = 0;
    FixedString<40> status_;
    f32 status_until_ = 0.0f;
};

} // namespace anom
