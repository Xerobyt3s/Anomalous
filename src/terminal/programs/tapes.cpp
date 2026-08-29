#include "terminal/programs/tapes.h"
#include "audio/tapes.h"
#include "carsys/carsys.h"
#include "terminal/screen.h"
#include "terminal/shell.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace anom {
namespace {

constexpr f32 kTapeWriteTime = 7.0f;
constexpr f32 kTapeDlTime = 11.0f;

} // namespace

bool TapesProgram::relay_linked(const TermView& view)
{
    return view.bus_state == PORT_LINKED && view.bus_tower && view.tower_breached;
}

i32 TapesProgram::build_list(const ProgramContext& ctx, const TermView& view, Entry* out,
                             i32 max) const
{
    const Fs& fs = ctx.shell->fs();
    i32 n = 0;
    for (i32 d = 0; d < kFsDriveCount && n < max; d++) {
        if (!fs.mounted(d)) {
            continue;
        }
        const FsDrive& drive = fs.drive(d);
        for (i32 i = 1; i < kFsDriveNodes && n < max; i++) {
            const FsNode& node = drive.nodes[i];
            if (!node.used || node.is_dir || node.trk <= 0) {
                continue;
            }
            out[n] = Entry{d, i, node.trk - 1, false};
            n++;
        }
    }
    if (relay_linked(view)) {
        for (i32 t = 0; t < static_cast<i32>(tapes_->count()) && n < max; t++) {
            if (!tapes_->on_relay(t)) {
                continue;
            }
            out[n] = Entry{-1, -1, t, true};
            n++;
        }
    }
    return n;
}

void TapesProgram::track_file_name(i32 track, FixedString<kFsNameMax + 1>& out) const
{
    char buf[kFsNameMax + 1] = {};
    u32 n = 0;
    for (const char c : tapes_->name(track)) {
        if (n >= 8) {
            break;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            buf[n++] = c;
        }
    }
    if (n == 0) {
        buf[n++] = 'T';
    }
    std::memcpy(buf + n, ".TRK", 5);
    out.assign(buf);
}

void TapesProgram::set_status(std::string_view msg)
{
    status_.assign(msg);
    status_until_ = timer_ + 3.2f;
}

void TapesProgram::enter(const ProgramContext& ctx, const TermView& view)
{
    (void)view;
    (void)ctx;
    timer_ = 0.0f;
    sel_ = 0;
    write_track_ = -1;
    dl_track_ = -1;
    prompt_track_ = -1;
    status_.clear();
    status_until_ = 0.0f;
}

bool TapesProgram::resolve_dest(const ProgramContext& ctx, i32 track)
{
    Fs& fs = ctx.shell->fs();
    const std::string_view path{dest_, dest_len_};
    const i32 pd = Fs::path_drive(path);
    if (path.size() >= 2 && path[1] == ':' && (pd < 0 || !fs.mounted(pd))) {
        set_status("DRIVE NOT READY");
        return false;
    }

    const FsRef cwd = ctx.shell->cwd();
    const FsRef dst = fs.resolve(cwd, path);
    FsRef dir{};
    FixedString<kFsNameMax + 1> name;

    if (fs.valid(dst) && fs.node(dst)->is_dir) {
        dir = dst;
        track_file_name(track, name);
        FsRef existing = fs.resolve(dir, name.view());
        for (u32 digit = 0; digit < 10 && fs.valid(existing); digit++) {
            char buf[kFsNameMax + 1] = {};
            std::memcpy(buf, name.c_str(), name.size());
            buf[name.size() - 5] = static_cast<char>('0' + digit);
            name.assign(buf);
            existing = fs.resolve(dir, name.view());
        }
        if (fs.valid(existing)) {
            set_status("TOO MANY COPIES IN TARGET DIR");
            return false;
        }
    } else if (fs.valid(dst)) {
        set_status("FILE EXISTS");
        return false;
    } else {
        FixedString<kTermInputMax + 1> head;
        FixedString<kTermInputMax + 1> tail;
        Shell::split_path(path, head, tail);
        if (head.empty()) {
            dir = cwd;
        } else {
            dir = fs.resolve(cwd, head.view());
            if (!fs.valid(dir) || !fs.node(dir)->is_dir) {
                set_status("PATH NOT FOUND");
                return false;
            }
        }
        if (tail.size() > kFsNameMax || !fs_name_valid(tail.view())) {
            set_status("BAD FILE NAME (8.3)");
            return false;
        }
        name.assign(tail.view());
    }

    if (fs.free_bytes(dir.drive) < tapes_->file_bytes(track)) {
        set_status("DISK FULL");
        return false;
    }

    dest_drive_ = dir.drive;
    dest_node_ = dir.node;
    dest_name_ = name;
    return true;
}

void TapesProgram::finish_download(const ProgramContext& ctx)
{
    const i32 track = dl_track_;
    dl_track_ = -1;

    Fs& fs = ctx.shell->fs();
    const FsRef dir{dest_drive_, dest_node_};
    if (!fs.valid(dir) || !fs.node(dir)->is_dir) {
        set_status("TARGET PATH LOST - DOWNLOAD LOST");
        return;
    }
    const FsRef file = fs.mkfile_rom(dir, dest_name_.view(), {}, tapes_->file_bytes(track),
                                     FsExe::None);
    if (!fs.valid(file)) {
        set_status("WRITE FAULT - DOWNLOAD LOST");
        return;
    }
    fs.node_mut(file)->trk = track + 1;
    set_status("DOWNLOAD COMPLETE");
}

void TapesProgram::submit(const ProgramContext& ctx, const TermView& view)
{
    Entry entries[kTapesListMax];
    const i32 count = build_list(ctx, view, entries, kTapesListMax);
    if (sel_ < 0 || sel_ >= count) {
        return;
    }

    const Entry& e = entries[sel_];
    if (e.archive) {
        FixedString<kFsNameMax + 1> name;
        track_file_name(e.track, name);
        prompt_track_ = e.track;
        const int written = std::snprintf(dest_, sizeof(dest_), "A:\\%s", name.c_str());
        dest_len_ = written > 0 ? static_cast<u32>(written) : 0;
        dest_cursor_ = dest_len_;
        ctx.screen->click();
        return;
    }

    const FsNode& node = ctx.shell->fs().drive(e.drive).nodes[e.node];
    if (node.corrupted) {
        set_status("TRACK DATA DAMAGED");
        return;
    }
    const CarSys& sys = *view.sys;
    if (!sys.parts[PART_COMPUTER].installed) {
        set_status("TERMINAL NOT DOCKED IN VEHICLE");
        return;
    }
    if (view.bus_tower) {
        set_status("BUS FEED IS RELAY NODE");
        return;
    }
    if (view.bus_state != PORT_LINKED) {
        set_status(view.bus_state == PORT_PLUGGED ? "BUS PORT NOT INITIALIZED - RUN LINK"
                                                  : "NO VEHICLE BUS CABLE");
        return;
    }
    if (sys.tape_inserted < 0) {
        set_status("NO CASSETTE IN DECK");
        return;
    }

    write_track_ = node.trk - 1;
    write_t_ = 0.0f;
    click_t_ = 0.0f;
    ctx.screen->click();
}

void TapesProgram::progress_bar(Screen& s, i32 row, f32 frac) const
{
    constexpr i32 kWidth = 46;
    s.grid_text(row, 6, TC_DIM, "[");
    const i32 filled = static_cast<i32>(frac * static_cast<f32>(kWidth));
    for (i32 i = 0; i < kWidth; i++) {
        const u16 glyph = i < filled ? '#' : (i == filled ? '>' : '.');
        s.grid_put(row, 7 + i, glyph, i < filled ? TC_GREEN : TC_DIM);
    }
    s.grid_text(row, 7 + kWidth, TC_DIM, "]");
    s.grid_text(row + 1, (static_cast<i32>(kTermCols) - 4) / 2, TC_BRIGHT, "%3.0f%%",
                static_cast<f64>(frac * 100.0f));
}

void TapesProgram::draw_write(const ProgramContext& ctx)
{
    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("TAPE ARCHIVE -- WRITING");
    const std::string_view name = tapes_->name(write_track_);
    s.grid_text(3, 4, TC_DIM, "SOURCE  %.*s", static_cast<int>(name.size()), name.data());
    s.grid_text(4, 4, TC_DIM, "TARGET  DECK CASSETTE");
    progress_bar(s, 9, f_clamp01(write_t_ / kTapeWriteTime));
    if (std::fmod(ctx.blink, 0.9f) < 0.55f) {
        s.grid_text(13, 4, TC_AMBER, "WRITE IN PROGRESS -- DO NOT EJECT");
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM, "[Q] ABORT");
}

void TapesProgram::draw_download(const ProgramContext& ctx)
{
    Screen& s = *ctx.screen;
    s.grid_clear();
    s.grid_title("RELAY LINK -- TRACK TRANSFER");

    char pathstr[kFsPathMax] = {};
    ctx.shell->fs().path_string(FsRef{dest_drive_, dest_node_}, pathstr, sizeof(pathstr));
    const std::size_t plen = std::strlen(pathstr);

    const std::string_view name = tapes_->name(dl_track_);
    s.grid_text(3, 4, TC_DIM, "SOURCE  RELAY :: %.*s", static_cast<int>(name.size()), name.data());
    s.grid_text(4, 4, TC_DIM, "TARGET  %s%s%s", pathstr,
                plen > 0 && pathstr[plen - 1] == '\\' ? "" : "\\", dest_name_.c_str());

    const f32 frac = f_clamp01(dl_t_ / kTapeDlTime);
    const u32 total = tapes_->file_bytes(dl_track_);
    s.grid_text(6, 4, TC_GREEN, "%7u / %7u BYTES",
                static_cast<u32>(frac * static_cast<f32>(total)), total);
    const i32 rate = 3200 + static_cast<i32>(std::fmod(ctx.blink * 7.3f, 1.0f) * 900.0f);
    s.grid_text(6, 40, TC_DIM, "%d B/S", rate);

    progress_bar(s, 9, frac);
    if (std::fmod(ctx.blink, 0.9f) < 0.55f) {
        s.grid_text(13, 4, TC_AMBER, "TRANSFER IN PROGRESS -- DO NOT DISCONNECT");
    }
    s.grid_text(static_cast<i32>(kTermRows) - 1, 1, TC_DIM, "[Q] ABORT");
}

void TapesProgram::draw_list(const ProgramContext& ctx, const TermView& view)
{
    Entry entries[kTapesListMax];
    count_ = build_list(ctx, view, entries, kTapesListMax);
    sel_ = sel_ >= count_ ? (count_ > 0 ? count_ - 1 : 0) : sel_;
    sel_ = sel_ < 0 ? 0 : sel_;

    Screen& s = *ctx.screen;
    const CarSys& sys = *view.sys;
    s.grid_clear();
    s.grid_title("TAPE ARCHIVE");

    if (!sys.parts[PART_COMPUTER].installed) {
        s.grid_text(2, 2, TC_RED, "DECK: TERMINAL NOT DOCKED");
    } else if (sys.tape_inserted < 0) {
        s.grid_text(2, 2, TC_DIM, "DECK: NO CASSETTE");
    } else {
        const std::string_view label = tapes_->label(sys.tape_inserted);
        s.grid_text(2, 2, TC_GREEN, "DECK: \"%.*s\" (%.0f%%)%s", static_cast<int>(label.size()),
                    label.data(), static_cast<f64>(sys.tape_cond * 100.0f),
                    sys.deck_play ? "  << ROLLING >>" : "");
    }

    const bool relay = relay_linked(view);
    s.grid_text(3, 2, relay ? TC_BRIGHT : TC_DIM,
                relay ? "RELAY ARCHIVE ONLINE" : "NO RELAY LINK -- DOWNLOADS OFFLINE");
    const bool buslink = view.bus_state == PORT_LINKED && !view.bus_tower;
    s.grid_text(3, 38, buslink ? TC_GREEN : TC_DIM,
                buslink ? "VEHICLE BUS ONLINE" : "NO VEHICLE BUS -- NO WRITES");

    if (count_ == 0) {
        s.grid_text(7, 4, TC_DIM, "NO TRACK FILES ON ANY MOUNTED DRIVE.");
        s.grid_text(8, 4, TC_DIM, "LINK A BREACHED RELAY NODE TO DOWNLOAD TRACKS.");
    }

    i32 first = sel_ - (kTapesListRows - 1);
    first = first < 0 ? 0 : first;
    for (i32 i = first; i < count_ && i - first < kTapesListRows; i++) {
        const i32 row = 5 + (i - first);
        const bool sel = i == sel_;
        if (sel) {
            s.grid_text(row, 2, TC_BRIGHT, ">");
        }
        const Entry& e = entries[i];
        const std::string_view name = tapes_->name(e.track);
        if (e.archive) {
            s.grid_text(row, 4, sel ? TC_BRIGHT : TC_AMBER, "RELAY %5uK  %.*s",
                        tapes_->file_bytes(e.track) / 1024, static_cast<int>(name.size()),
                        name.data());
        } else {
            const FsNode& node = ctx.shell->fs().drive(e.drive).nodes[e.node];
            s.grid_text(row, 4, sel ? TC_BRIGHT : TC_GREEN, "%c:    %5uK  %.*s%s",
                        'A' + e.drive, node.size / 1024, static_cast<int>(name.size()),
                        name.data(), node.corrupted ? " [DMG]" : "");
        }
    }

    const i32 last_row = static_cast<i32>(kTermRows) - 1;
    if (prompt_track_ >= 0) {
        const std::string_view track_name = tapes_->name(prompt_track_);
        s.grid_text(last_row - 3, 2, TC_BRIGHT, "STORE \"%.*s\" AS:",
                    static_cast<int>(track_name.size()), track_name.data());
        s.grid_text(last_row - 2, 2, TC_GREEN, "%s", dest_);
        if (std::fmod(ctx.blink, 1.06f) < 0.53f) {
            i32 ccol = 2 + static_cast<i32>(dest_cursor_);
            ccol = ccol > static_cast<i32>(kTermCols) - 1 ? static_cast<i32>(kTermCols) - 1 : ccol;
            s.grid_put(last_row - 2, ccol, 127, TC_BRIGHT);
        }
        if (!status_.empty() && timer_ < status_until_) {
            s.grid_text(last_row - 1, 2, TC_AMBER, "%s", status_.c_str());
        }
        s.grid_text(last_row, 1, TC_DIM,
                    "[ENTER] START TRANSFER   [ENTER] ON EMPTY LINE CANCELS");
        return;
    }

    if (!status_.empty() && timer_ < status_until_) {
        s.grid_text(last_row - 2, 2, TC_AMBER, "%s", status_.c_str());
    }
    s.grid_text(last_row, 1, TC_DIM,
                "[UP/DN] SELECT   [ENTER] WRITE TO CASSETTE / DOWNLOAD   [Q] EXIT");
}

void TapesProgram::update(const ProgramContext& ctx, const TermView& view, f32 dt)
{
    timer_ += dt;

    if (write_track_ >= 0) {
        const CarSys& sys = *view.sys;
        if (view.bus_state != PORT_LINKED || view.bus_tower) {
            write_track_ = -1;
            set_status("WRITE FAILED -- BUS LINK LOST");
        } else if (!sys.parts[PART_COMPUTER].installed || sys.tape_inserted < 0) {
            write_track_ = -1;
            set_status("WRITE FAILED -- DECK LOST");
        } else {
            write_t_ += dt;
            click_t_ -= dt;
            if (click_t_ <= 0.0f) {
                click_t_ = 0.8f;
                ctx.screen->click();
            }
            if (write_t_ >= kTapeWriteTime) {
                ctx.request->tape_write = true;
                ctx.request->tape_write_value = write_track_;
                write_track_ = -1;
                set_status("WRITE COMPLETE");
            } else {
                draw_write(ctx);
                return;
            }
        }
    }

    if (dl_track_ >= 0) {
        if (!relay_linked(view)) {
            dl_track_ = -1;
            set_status("RELAY LINK LOST -- TRANSFER ABORTED");
        } else {
            dl_t_ += dt;
            if (dl_t_ >= kTapeDlTime) {
                finish_download(ctx);
            } else {
                draw_download(ctx);
                return;
            }
        }
    }

    draw_list(ctx, view);
}

void TapesProgram::key_char(const ProgramContext& ctx, const TermView& view, char c)
{
    (void)view;
    if (prompt_track_ < 0 || dest_len_ + 1 >= sizeof(dest_)) {
        return;
    }
    std::memmove(&dest_[dest_cursor_ + 1], &dest_[dest_cursor_],
                 dest_len_ - dest_cursor_ + 1);
    dest_[dest_cursor_++] = c;
    dest_len_++;
    ctx.screen->click();
}

bool TapesProgram::key(const ProgramContext& ctx, const TermView& view, TermKey k)
{
    if (prompt_track_ >= 0) {
        switch (k) {
        case TermKey::Backspace:
            if (dest_cursor_ > 0) {
                std::memmove(&dest_[dest_cursor_ - 1], &dest_[dest_cursor_],
                             dest_len_ - dest_cursor_ + 1);
                dest_cursor_--;
                dest_len_--;
                ctx.screen->click();
            }
            break;
        case TermKey::Delete:
            if (dest_cursor_ < dest_len_) {
                std::memmove(&dest_[dest_cursor_], &dest_[dest_cursor_ + 1],
                             dest_len_ - dest_cursor_);
                dest_len_--;
                ctx.screen->click();
            }
            break;
        case TermKey::Left:
            dest_cursor_ -= dest_cursor_ > 0 ? 1 : 0;
            break;
        case TermKey::Right:
            dest_cursor_ += dest_cursor_ < dest_len_ ? 1 : 0;
            break;
        case TermKey::Home:
            dest_cursor_ = 0;
            break;
        case TermKey::End:
            dest_cursor_ = dest_len_;
            break;
        case TermKey::Enter:
            if (dest_len_ == 0) {
                prompt_track_ = -1;
            } else if (!relay_linked(view)) {
                set_status("RELAY LINK LOST");
                prompt_track_ = -1;
            } else if (resolve_dest(ctx, prompt_track_)) {
                dl_track_ = prompt_track_;
                prompt_track_ = -1;
                dl_t_ = 0.0f;
                ctx.screen->click();
            }
            break;
        default:
            break;
        }
        return false;
    }

    if (dl_track_ >= 0 || write_track_ >= 0) {
        if (k == TermKey::Quit || k == TermKey::Escape) {
            if (dl_track_ >= 0) {
                dl_track_ = -1;
                set_status("TRANSFER ABORTED");
            }
            if (write_track_ >= 0) {
                write_track_ = -1;
                set_status("WRITE ABORTED");
            }
        }
        return false;
    }

    switch (k) {
    case TermKey::Up:
        if (sel_ > 0) {
            sel_--;
            ctx.screen->click();
        }
        break;
    case TermKey::Down:
        if (sel_ + 1 < count_) {
            sel_++;
            ctx.screen->click();
        }
        break;
    case TermKey::Enter:
        submit(ctx, view);
        break;
    case TermKey::Quit:
    case TermKey::Escape:
        return true;
    default:
        break;
    }
    return false;
}

} // namespace anom
