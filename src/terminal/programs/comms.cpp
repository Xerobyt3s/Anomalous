#include "terminal/programs/comms.h"
#include "terminal/comms.h"
#include "terminal/screen.h"

#include <cmath>

namespace anom {
namespace {

constexpr f32 kConnectCps = 46.0f;
constexpr f32 kConnectHold = 0.7f;
constexpr f32 kSpeechCps = 26.0f;
constexpr f32 kSpriteScanTime = 1.1f;

i32 printable_in(std::string_view text, i32 budget)
{
    i32 printable = 0;
    for (const char c : text) {
        if (budget <= 0) {
            break;
        }
        if (c != '\n') {
            budget--;
            printable += c != ' ' ? 1 : 0;
        }
    }
    return printable;
}

i32 text_chars(std::string_view text)
{
    i32 n = 0;
    for (const char c : text) {
        n += c != '\n' ? 1 : 0;
    }
    return n;
}

void att_tags(const CommsMsg& msg, FixedString<16>& out)
{
    out.clear();
    if (!msg.att_site.empty()) {
        out.append("LOC ");
    }
    if (msg.has_briefing) {
        out.append("BRF ");
    }
    if (!msg.att_part.empty()) {
        out.append("PKG ");
    }
}

} // namespace

void CommsProgram::set_phase(Phase phase)
{
    phase_ = phase;
    phase_time_ = 0.0f;
    if (phase == Phase::Inbox || phase == Phase::Brief) {
        materialize_ = 0.0f;
    }
    if (phase == Phase::Brief) {
        reveal_ = 0.0f;
        blips_ = 0;
    }
}

void CommsProgram::set_status(std::string_view text)
{
    status_.assign(text);
    status_until_ = 5.0f;
}

void CommsProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)ctx;
    (void)view;
    open_ = 0;
    page_ = 0;
    status_.clear();
    status_until_ = 0.0f;
    set_phase(Phase::Connect);
}

f32 CommsProgram::pixelate() const
{
    if (phase_ == Phase::Connect || materialize_ >= 1.0f) {
        return 1.0f;
    }
    return static_cast<f32>(64 >> static_cast<i32>(materialize_ * 7.0f));
}

u32 CommsProgram::connect_lines(FixedString<64>* out) const
{
    out[0].assign("DR RELAYNET TERMINAL 2.3");
    out[1].assign("ALIGNING WHIP ANTENNA ............ LOCK");
    out[2].assign("GARAGE RELAY HANDSHAKE ........... CARRIER OK");
    out[3].assign("CRYPT KEY 'LONG PATIENCE' ........ ACCEPTED");
    out[4].format("MAILBOX SYNC ..................... %d STORED / %d NEW",
                  mail_->delivered_count(), mail_->unread_count());
    return 5;
}

void CommsProgram::draw_connect(const ProgramContext& ctx, f32 dt)
{
    FixedString<64> lines[5];
    const u32 n = connect_lines(lines);
    i32 total = 0;
    for (u32 i = 0; i < n; i++) {
        total += static_cast<i32>(lines[i].size());
    }

    const i32 before = static_cast<i32>((phase_time_ - dt) * kConnectCps);
    const i32 after = static_cast<i32>(phase_time_ * kConnectCps);
    if (after > before && before < total) {
        ctx.screen->click();
    }

    Screen& s = *ctx.screen;
    i32 budget = after;
    i32 row = 2;
    for (u32 i = 0; i < n && budget > 0; i++) {
        s.grid_block(row, 3, 1, i == 0 ? TC_BRIGHT : TC_GREEN, lines[i].view(), budget);
        budget -= static_cast<i32>(lines[i].size());
        row += i == 0 ? 2 : 1;
    }
    if (std::fmod(ctx.blink, 0.5f) < 0.3f) {
        s.grid_put(row + 1, 3, 127, TC_BRIGHT);
    }

    if (phase_time_ * kConnectCps > static_cast<f32>(total) + kConnectHold * kConnectCps) {
        set_phase(Phase::Inbox);
    }
}

void CommsProgram::draw_inbox(const ProgramContext& ctx)
{
    Screen& s = *ctx.screen;
    s.grid_text(0, 1, TC_BRIGHT,
                "== RELAYNET // MAILBOX ==============================================");
    s.grid_text(1, 1, TC_GREEN,
                "CARRIER: GARAGE RELAY  LINK: GREEN  TRAFFIC: %d STORED / %d UNREAD",
                mail_->delivered_count(), mail_->unread_count());

    const i32 total = mail_->delivered_count();
    if (total == 0) {
        s.grid_text(4, 1, TC_GREEN, "NO TRAFFIC ON THE RELAY.");
    }

    i32 row = 3;
    for (i32 i = 1; i <= total && row < static_cast<i32>(kTermRows) - 2; i++) {
        const CommsMsg* msg = mail_->get(i);
        if (!msg) {
            continue;
        }
        FixedString<16> tags;
        att_tags(*msg, tags);
        const bool unread = !msg->read;
        u8 color = unread ? TC_BRIGHT : TC_DIM;
        if (unread && std::fmod(ctx.blink + static_cast<f32>(i) * 0.13f, 1.1f) < 0.12f) {
            color = TC_GREEN;
        }
        s.grid_text(row, 2, color, "[%d]%c C%02d %-12.*s %-28.28s %s", i, unread ? '*' : ' ',
                    msg->cycle, static_cast<int>(msg->from.size()), msg->from.data(),
                    msg->sub.data(), tags.c_str());
        row++;
    }

    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM,
                "[1-9] OPEN TRANSMISSION      [Q] DISCONNECT");
}

void CommsProgram::draw_read(const ProgramContext& ctx)
{
    const CommsMsg* msg = mail_->get(open_);
    if (!msg) {
        return;
    }

    Screen& s = *ctx.screen;
    s.grid_text(0, 1, TC_BRIGHT,
                "== RELAYNET // TRANSMISSION %02d =====================================", open_);
    s.grid_text(1, 1, TC_GREEN, "FROM: %-18.*s CYCLE %02d", static_cast<int>(msg->from.size()),
                msg->from.data(), msg->cycle);
    s.grid_text(2, 1, TC_GREEN, "SUBJ: %.*s", static_cast<int>(msg->sub.size()),
                msg->sub.data());

    s.grid_block(4, 1, 12, TC_GREEN, msg->body, -1);

    const bool any_att = !msg->att_site.empty() || msg->has_briefing || !msg->att_part.empty();
    i32 ay = 17;
    if (any_att) {
        s.grid_text(ay++, 1, TC_BRIGHT, "ATTACHMENTS:");
        if (msg->has_briefing) {
            s.grid_text(ay++, 1, std::fmod(ctx.blink, 1.0f) < 0.6f ? TC_BRIGHT : TC_GREEN,
                        "  [B] RECORDED BRIEFING: %.*s",
                        static_cast<int>(msg->briefing.speaker.size()),
                        msg->briefing.speaker.data());
        }
        if (!msg->att_site.empty()) {
            const int len = static_cast<int>(msg->att_site.size());
            if (msg->site_downloaded) {
                s.grid_text(ay++, 1, TC_DIM, "      SITE COORDINATES: %.*s (ON FILE)", len,
                            msg->att_site.data());
            } else {
                s.grid_text(ay++, 1, TC_BRIGHT, "  [L] SITE COORDINATES: %.*s -- DOWNLOAD", len,
                            msg->att_site.data());
            }
        }
        if (!msg->att_part.empty()) {
            const int len = static_cast<int>(msg->att_part.size());
            if (msg->part_claimed) {
                s.grid_text(ay++, 1, TC_DIM, "      SUPPLY VOUCHER: %.*s (CLAIMED)", len,
                            msg->att_part.data());
            } else {
                s.grid_text(ay++, 1, TC_BRIGHT, "  [P] SUPPLY VOUCHER: %.*s %.0f%% -- CLAIM", len,
                            msg->att_part.data(), static_cast<f64>(msg->att_part_cond));
            }
        }
    }

    if (!status_.empty() && status_until_ > 0.0f && std::fmod(ctx.blink, 0.6f) < 0.4f) {
        s.grid_text(static_cast<i32>(kTermRows) - 2, 1, TC_AMBER, "%s", status_.c_str());
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM, "[Q] BACK TO MAILBOX");
}

void CommsProgram::frame(Screen& s, i32 row0, i32 col0, i32 cols, i32 rows, i32 title_cols)
{
    for (i32 c = 0; c < cols; c++) {
        if (c < 2 || c > title_cols + 2) {
            s.grid_put(row0, col0 + c, '-', TC_GREEN);
        }
        s.grid_put(row0 + rows - 1, col0 + c, '-', TC_GREEN);
    }
    for (i32 r = 1; r < rows - 1; r++) {
        s.grid_put(row0 + r, col0, '|', TC_GREEN);
        s.grid_put(row0 + r, col0 + cols - 1, '|', TC_GREEN);
    }
    s.grid_put(row0, col0, '+', TC_GREEN);
    s.grid_put(row0, col0 + cols - 1, '+', TC_GREEN);
    s.grid_put(row0 + rows - 1, col0, '+', TC_GREEN);
    s.grid_put(row0 + rows - 1, col0 + cols - 1, '+', TC_GREEN);
}

void CommsProgram::draw_brief(const ProgramContext& ctx, f32 dt)
{
    const CommsMsg* msg = mail_->get(open_);
    if (!msg) {
        set_phase(Phase::Read);
        return;
    }

    const CommsBriefing& brief = msg->briefing;
    const std::string_view text = msg->page(page_);
    const i32 total = text_chars(text);
    const bool talking = phase_time_ > kSpriteScanTime;

    if (talking && static_cast<i32>(reveal_) < total) {
        reveal_ += dt * kSpeechCps;
        const i32 spoken = printable_in(text, static_cast<i32>(reveal_));
        if (spoken / 2 > blips_) {
            blips_ = spoken / 2;
            ctx.screen->click();
        }
    }

    Screen& s = *ctx.screen;
    FixedString<64> title;
    title.format(" RELAYNET RECORDED BRIEFING -- %.*s ",
                 static_cast<int>(brief.speaker.size()), brief.speaker.data());
    frame(s, 0, 1, 68, static_cast<i32>(kTermRows), static_cast<i32>(title.size()));
    s.grid_text(0, 4, TC_BRIGHT, "%s", title.c_str());
    if (std::fmod(ctx.blink, 1.0f) < 0.55f) {
        s.grid_text(2, 60, TC_BRIGHT, "\x7f REC");
    }

    const i32 sprite_rows = static_cast<i32>(brief.sprite_rows);
    i32 rows_visible = static_cast<i32>(phase_time_ / kSpriteScanTime
                                        * static_cast<f32>(sprite_rows));
    rows_visible = rows_visible > sprite_rows ? sprite_rows : rows_visible;

    const bool speaking = talking && static_cast<i32>(reveal_) < total;
    for (i32 r = 0; r < rows_visible; r++) {
        u8 color = TC_GREEN;
        i32 jitter = 0;
        if (r == rows_visible - 1 && rows_visible < sprite_rows) {
            color = TC_BRIGHT;
        }
        if (speaking && r > sprite_rows * 2 / 3) {
            color = f_abs(std::sin(ctx.blink * 26.0f + static_cast<f32>(r) * 1.7f)) > 0.55f
                      ? TC_GREEN
                      : TC_DIM;
            jitter = std::fmod(ctx.blink * 31.0f + static_cast<f32>(r), 2.0f) < 0.2f ? 1 : 0;
        }
        s.grid_block(3 + r, 4 + jitter, 1, color, brief.sprite[r], -1);
    }

    if (talking) {
        s.grid_block(4, 37, static_cast<i32>(kTermRows) - 8, TC_BRIGHT, text,
                     static_cast<i32>(reveal_));
    }

    s.grid_text(static_cast<i32>(kTermRows) - 2, 4, TC_DIM, "PAGE %d/%d", page_ + 1,
                static_cast<i32>(brief.page_count));
    if (talking && static_cast<i32>(reveal_) >= total && std::fmod(ctx.blink, 0.7f) < 0.45f) {
        s.grid_text(static_cast<i32>(kTermRows) - 2, 16, TC_BRIGHT, "%s",
                    static_cast<u32>(page_ + 1) < brief.page_count ? "[ENTER] CONTINUE"
                                                                   : "[ENTER] END OF RECORDING");
    }
}

void CommsProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    (void)view;
    phase_time_ += dt;
    materialize_ += dt;
    if (status_until_ > 0.0f) {
        status_until_ -= dt;
    }

    ctx.screen->grid_clear();
    switch (phase_) {
    case Phase::Connect:
        draw_connect(ctx, dt);
        break;
    case Phase::Inbox:
        draw_inbox(ctx);
        break;
    case Phase::Read:
        draw_read(ctx);
        break;
    case Phase::Brief:
        draw_brief(ctx, dt);
        break;
    }
}

void CommsProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)view;

    if (phase_ == Phase::Inbox && c >= '1' && c <= '9') {
        CommsMsg* msg = mail_->get(c - '0');
        if (!msg) {
            return;
        }
        open_ = c - '0';
        page_ = 0;
        set_phase(Phase::Read);
        materialize_ = 10.0f;
        ctx.screen->click();
        msg->read = true;
        return;
    }
    if (phase_ != Phase::Read) {
        return;
    }

    CommsMsg* msg = mail_->get(open_);
    if (!msg) {
        return;
    }
    if (c == 'B' && msg->has_briefing) {
        page_ = 0;
        set_phase(Phase::Brief);
    } else if (c == 'L' && !msg->att_site.empty() && !msg->site_downloaded) {
        msg->site_downloaded = true;
        set_status("COORDINATES SAVED TO SURVEY.");
    } else if (c == 'P' && !msg->att_part.empty() && !msg->part_claimed) {
        msg->part_claimed = true;
        set_status("SUPPLY VOUCHER LOGGED.");
    }
}

bool CommsProgram::key(const ProgramContext& ctx, const TermView& view, TermKey k)
{
    (void)ctx;
    (void)view;

    if (k == TermKey::Enter) {
        if (phase_ != Phase::Brief) {
            return false;
        }
        const CommsMsg* msg = mail_->get(open_);
        if (!msg) {
            set_phase(Phase::Read);
            return false;
        }
        const i32 total = text_chars(msg->page(page_));
        if (phase_time_ > kSpriteScanTime && static_cast<i32>(reveal_) < total) {
            reveal_ = static_cast<f32>(total);
            return false;
        }
        if (static_cast<u32>(page_ + 1) < msg->briefing.page_count) {
            page_++;
            reveal_ = 0.0f;
            blips_ = 0;
        } else {
            set_phase(Phase::Read);
            materialize_ = 10.0f;
        }
        return false;
    }

    if (k != TermKey::Quit && k != TermKey::Escape) {
        return false;
    }
    if (phase_ == Phase::Brief) {
        set_phase(Phase::Read);
        materialize_ = 10.0f;
        return false;
    }
    if (phase_ == Phase::Read) {
        set_phase(Phase::Inbox);
        materialize_ = 10.0f;
        return false;
    }
    return true;
}

} // namespace anom
